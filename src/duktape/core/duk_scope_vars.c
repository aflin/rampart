/*
 *  duk_rp_get_scope_vars(): Inspect variables at a given call stack level.
 *
 *  This file is #include'd into duktape.c so that it has access to all
 *  internal types (duk_hdecenv, duk_hobjenv, duk_hcompfunc, duk_activation).
 *
 *  Two modes:
 *
 *  1) varname == NULL:  Push a plain object { name: value, ... } containing
 *     all variables belonging to the scope 'type' (DUK_SCOPE_LOCAL, etc.).
 *     For DUK_SCOPE_CLOSURE, multiple parent declarative envs are merged
 *     (inner scope wins on name collisions).
 *     For DUK_SCOPE_WITH / DUK_SCOPE_GLOBAL, the target object itself is
 *     pushed (not a copy).
 *
 *  2) varname != NULL:  Search all scopes (innermost first) for 'varname',
 *     ignoring the 'type' parameter.  If found, push { value: <val>,
 *     scope: "local"|"closure"|"with"|"global" }.  If not found, push
 *     undefined.
 *
 *  call_stack_level uses the same convention as duk_inspect_callstack_entry():
 *    -1 = topmost activation (the C function calling this)
 *    -2 = its caller
 */

#define DUK_SCOPE_LOCAL   0
#define DUK_SCOPE_CLOSURE 1
#define DUK_SCOPE_WITH    2
#define DUK_SCOPE_GLOBAL  3

/* ------------------------------------------------------------------ */
/*  Helpers: read register value from a varmap entry                   */
/* ------------------------------------------------------------------ */

/* Get register number from a varmap tval (always a number). */
DUK_LOCAL duk_uint_t duk__scope_get_regnum(duk_tval *tv) {
#if defined(DUK_USE_FASTINT)
	return (duk_uint_t) DUK_TVAL_GET_FASTINT_U32(tv);
#else
	return (duk_uint_t) DUK_TVAL_GET_NUMBER(tv);
#endif
}

/* Compute pointer to a register value given a base and register number. */
DUK_LOCAL duk_tval *duk__scope_regval(duk_tval *valstack, duk_size_t base_byteoff, duk_uint_t regnum) {
	return (duk_tval *) (void *) ((duk_uint8_t *) valstack +
	       base_byteoff + sizeof(duk_tval) * regnum);
}

/* ------------------------------------------------------------------ */
/*  Helpers: add all vars from a scope into an object on the stack     */
/* ------------------------------------------------------------------ */

/* Add vars from an open declarative env (registers via varmap). */
DUK_LOCAL void duk__scope_add_open_vars(duk_hthread *thr, duk_hdecenv *decenv, duk_idx_t obj_idx, duk_bool_t skip_existing) {
	duk_hobject *varmap = decenv->varmap;
	duk_hobject *env = (duk_hobject *) decenv;
	duk_uint_fast32_t i;

	/* First: register-bound variables via the varmap. */
	for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ENEXT(varmap); i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(thr->heap, varmap, i);
		duk_tval *tv;
		duk_tval *regval;

		if (key == NULL) {
			continue;
		}
		if (skip_existing) {
			duk_push_hstring(thr, key);
			if (duk_has_prop((duk_context *) thr, obj_idx)) {
				continue; /* key consumed by duk_has_prop */
			}
		}

		tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(thr->heap, varmap, i);
		DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
		regval = duk__scope_regval(decenv->thread->valstack,
		                           decenv->regbase_byteoff,
		                           duk__scope_get_regnum(tv));

		duk_push_hstring(thr, key);
		duk_push_tval(thr, regval);
		duk_put_prop((duk_context *) thr, obj_idx);
	}

	/* Second: non-register properties on the env object (e.g. from
	 * rampart.localize).  Skip hidden keys, accessors, and any name
	 * that is in the varmap (those were already added from registers
	 * above — the env object holds stale preallocated values for them).
	 */
	for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ENEXT(env); i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(thr->heap, env, i);
		duk_tval *tv;

		if (key == NULL) {
			continue;
		}
		if (DUK_HSTRING_HAS_HIDDEN(key)) {
			continue;
		}
		if (DUK_HOBJECT_E_SLOT_IS_ACCESSOR(thr->heap, env, i)) {
			continue;
		}
		/* Skip if this name is in the varmap (register-bound). */
		if (duk_hobject_find_entry_tval_ptr(thr->heap, varmap, key) != NULL) {
			continue;
		}

		tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(thr->heap, env, i);
		duk_push_hstring(thr, key);
		duk_push_tval(thr, tv);
		duk_put_prop((duk_context *) thr, obj_idx);
	}
}

/* Add vars from activation registers (delayed env case). */
DUK_LOCAL void duk__scope_add_act_vars(duk_hthread *thr, duk_activation *act, duk_hobject *varmap, duk_idx_t obj_idx) {
	duk_uint_fast32_t i;

	for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ENEXT(varmap); i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(thr->heap, varmap, i);
		duk_tval *tv;
		duk_tval *regval;

		if (key == NULL) {
			continue;
		}

		tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(thr->heap, varmap, i);
		DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
		regval = duk__scope_regval(thr->valstack,
		                           act->bottom_byteoff,
		                           duk__scope_get_regnum(tv));

		duk_push_hstring(thr, key);
		duk_push_tval(thr, regval);
		duk_put_prop((duk_context *) thr, obj_idx);
	}
}

/* Add vars from a closed declarative env (own properties). */
DUK_LOCAL void duk__scope_add_closed_vars(duk_hthread *thr, duk_hobject *env, duk_idx_t obj_idx, duk_bool_t skip_existing) {
	duk_uint_fast32_t i;

	for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ENEXT(env); i++) {
		duk_hstring *key = DUK_HOBJECT_E_GET_KEY(thr->heap, env, i);
		duk_tval *tv;

		if (key == NULL) {
			continue;
		}
		if (DUK_HSTRING_HAS_HIDDEN(key)) {
			continue;
		}
		if (DUK_HOBJECT_E_SLOT_IS_ACCESSOR(thr->heap, env, i)) {
			continue;
		}
		if (skip_existing) {
			duk_push_hstring(thr, key);
			if (duk_has_prop((duk_context *) thr, obj_idx)) {
				continue;
			}
		}

		tv = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(thr->heap, env, i);
		duk_push_hstring(thr, key);
		duk_push_tval(thr, tv);
		duk_put_prop((duk_context *) thr, obj_idx);
	}
}

/* ------------------------------------------------------------------ */
/*  Helpers: look up a single variable in a scope                      */
/* ------------------------------------------------------------------ */

/* Look up varname in an open declarative env.  Returns 1 and pushes value if found.
 * Checks registers (via varmap) first, then env object properties (for localized names).
 */
DUK_LOCAL duk_bool_t duk__scope_find_open(duk_hthread *thr, duk_hdecenv *decenv, duk_hstring *h_name) {
	duk_tval *tv = duk_hobject_find_entry_tval_ptr(thr->heap, decenv->varmap, h_name);
	if (tv != NULL) {
		duk_tval *regval;
		DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
		regval = duk__scope_regval(decenv->thread->valstack,
		                           decenv->regbase_byteoff,
		                           duk__scope_get_regnum(tv));
		duk_push_tval(thr, regval);
		return 1;
	}
	/* Not in varmap — check env object properties (e.g. from rampart.localize). */
	tv = duk_hobject_find_entry_tval_ptr(thr->heap, (duk_hobject *) decenv, h_name);
	if (tv != NULL) {
		duk_push_tval(thr, tv);
		return 1;
	}
	return 0;
}

/* Look up varname in activation registers (delayed env). Returns 1 and pushes value if found. */
DUK_LOCAL duk_bool_t duk__scope_find_act(duk_hthread *thr, duk_activation *act, duk_hobject *varmap, duk_hstring *h_name) {
	duk_tval *tv = duk_hobject_find_entry_tval_ptr(thr->heap, varmap, h_name);
	if (tv != NULL) {
		duk_tval *regval;
		DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
		regval = duk__scope_regval(thr->valstack,
		                           act->bottom_byteoff,
		                           duk__scope_get_regnum(tv));
		duk_push_tval(thr, regval);
		return 1;
	}
	return 0;
}

/* Look up varname in a closed declarative env.  Returns 1 and pushes value if found. */
DUK_LOCAL duk_bool_t duk__scope_find_closed(duk_hthread *thr, duk_hobject *env, duk_hstring *h_name) {
	duk_tval *tv = duk_hobject_find_entry_tval_ptr(thr->heap, env, h_name);
	if (tv != NULL) {
		duk_push_tval(thr, tv);
		return 1;
	}
	return 0;
}

/* Look up varname in an object env target.  Returns 1 and pushes value if found. */
DUK_LOCAL duk_bool_t duk__scope_find_objenv(duk_hthread *thr, duk_hobject *target, duk_hstring *h_name) {
	if (duk_hobject_hasprop_raw(thr, target, h_name)) {
		duk_tval tv_target, tv_key;
		DUK_TVAL_SET_OBJECT(&tv_target, target);
		DUK_TVAL_SET_STRING(&tv_key, h_name);
		(void) duk_hobject_getprop(thr, &tv_target, &tv_key); /* pushes value */
		return 1;
	}
	return 0;
}

/* ------------------------------------------------------------------ */
/*  Scope type classification                                          */
/* ------------------------------------------------------------------ */

DUK_LOCAL const char *duk__scope_type_str(int type_id) {
	switch (type_id) {
	case DUK_SCOPE_LOCAL:   return "local";
	case DUK_SCOPE_CLOSURE: return "closure";
	case DUK_SCOPE_WITH:    return "with";
	case DUK_SCOPE_GLOBAL:  return "global";
	default:                return "unknown";
	}
}

/* Classify an environment record.  is_first_decenv should be 1 for the
 * first declarative env encountered (the local scope). */
DUK_LOCAL int duk__scope_classify(duk_hthread *thr, duk_hobject *env, duk_bool_t is_first_decenv) {
	duk_small_uint_t cl = DUK_HOBJECT_GET_CLASS_NUMBER(env);
	if (cl == DUK_HOBJECT_CLASS_DECENV) {
		return is_first_decenv ? DUK_SCOPE_LOCAL : DUK_SCOPE_CLOSURE;
	}
	/* Must be OBJENV */
	if (env == thr->builtins[DUK_BIDX_GLOBAL_ENV]) {
		return DUK_SCOPE_GLOBAL;
	}
	return DUK_SCOPE_WITH;
}

/* ------------------------------------------------------------------ */
/*  Main entry point                                                   */
/* ------------------------------------------------------------------ */

DUK_EXTERNAL void duk_rp_get_scope_vars(duk_context *ctx, duk_int_t call_stack_level, int type, const char *varname) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;
	duk_hobject *env;
	duk_hobject *func;
	duk_uint_t sanity;
	duk_bool_t is_first_decenv = 1;

	DUK_ASSERT_API_ENTRY(thr);

	act = duk_hthread_get_activation_for_level(thr, call_stack_level);
	if (act == NULL) {
		duk_push_undefined(ctx);
		return;
	}

	func = DUK_ACT_GET_FUNC(act);
	env = act->lex_env;

	/* -------------------------------------------------------------- */
	/*  MODE 1: single variable lookup (varname != NULL)               */
	/* -------------------------------------------------------------- */

	if (varname != NULL) {
		duk_hstring *h_name;

		/* Intern the name once; stays on stack to prevent GC. */
		duk_push_string(ctx, varname);
		h_name = duk_known_hstring(thr, -1);

		/* Delayed env: check activation registers first. */
		if (env == NULL && func != NULL && DUK_HOBJECT_IS_COMPFUNC(func)) {
			duk_hobject *varmap = duk_hobject_get_varmap(thr, func);

			if (varmap != NULL && duk__scope_find_act(thr, act, varmap, h_name)) {
				/* stack: [ ... h_name value ] */
				duk_remove(ctx, -2);           /* remove h_name */
				goto found_var;                /* value on top, scope = "local" */
			}
			is_first_decenv = 0;

			env = DUK_HCOMPFUNC_GET_LEXENV(thr->heap, (duk_hcompfunc *) func);
			if (env == NULL) {
				env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
			}
		}

		/* Walk the scope chain. */
		sanity = DUK_HOBJECT_PROTOTYPE_CHAIN_SANITY;
		while (env != NULL && sanity-- > 0) {
			int scope_id = duk__scope_classify(thr, env, is_first_decenv);
			duk_small_uint_t cl = DUK_HOBJECT_GET_CLASS_NUMBER(env);
			duk_bool_t found = 0;

			if (cl == DUK_HOBJECT_CLASS_DECENV) {
				duk_hdecenv *decenv = (duk_hdecenv *) env;
				if (decenv->thread != NULL && decenv->varmap != NULL) {
					found = duk__scope_find_open(thr, decenv, h_name);
				} else {
					found = duk__scope_find_closed(thr, env, h_name);
				}
				if (is_first_decenv) {
					is_first_decenv = 0;
				}
			} else {
				duk_hobject *target = ((duk_hobjenv *) env)->target;
				found = duk__scope_find_objenv(thr, target, h_name);
			}

			if (found) {
				/* stack: [ ... h_name value ] */
				duk_remove(ctx, -2);   /* remove h_name */
			found_var:
				/* stack: [ ... value ]  — build result object */
				{
					const char *stype = duk__scope_type_str(
					    env ? duk__scope_classify(thr, env, 0)
					        : DUK_SCOPE_LOCAL /* delayed-env hit */);

					/* For the delayed env hit we already removed h_name
					 * and jumped here with scope_id effectively LOCAL.
					 * For the normal case we determined scope_id above. */
					if (env != NULL) {
						stype = duk__scope_type_str(scope_id);
					} else {
						stype = "local";
					}

					duk_push_bare_object(ctx);      /* result */
					duk_pull(ctx, -2);               /* move value into position */
					duk_put_prop_string(ctx, -2, "value");
					duk_push_string(ctx, stype);
					duk_put_prop_string(ctx, -2, "scope");
					return;
				}
			}

			env = DUK_HOBJECT_GET_PROTOTYPE(thr->heap, env);
		}

		/* Not found — pop h_name, push undefined. */
		duk_pop(ctx);
		duk_push_undefined(ctx);
		return;
	}

	/* -------------------------------------------------------------- */
	/*  MODE 2: all variables for a given scope type                   */
	/* -------------------------------------------------------------- */

	/* For with/global we push the target object directly.
	 * For local/closure we build a new {name: value} object. */

	if (type == DUK_SCOPE_WITH || type == DUK_SCOPE_GLOBAL) {
		/* Delayed env: skip to parent. */
		if (env == NULL && func != NULL && DUK_HOBJECT_IS_COMPFUNC(func)) {
			env = DUK_HCOMPFUNC_GET_LEXENV(thr->heap, (duk_hcompfunc *) func);
			if (env == NULL) {
				env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
			}
		}

		sanity = DUK_HOBJECT_PROTOTYPE_CHAIN_SANITY;
		while (env != NULL && sanity-- > 0) {
			int scope_id = duk__scope_classify(thr, env, is_first_decenv);
			if (DUK_HOBJECT_GET_CLASS_NUMBER(env) == DUK_HOBJECT_CLASS_DECENV) {
				if (is_first_decenv) {
					is_first_decenv = 0;
				}
			} else if (scope_id == type) {
				duk_push_hobject(thr, ((duk_hobjenv *) env)->target);
				return;
			}
			env = DUK_HOBJECT_GET_PROTOTYPE(thr->heap, env);
		}
		/* Not found — push undefined. */
		duk_push_undefined(ctx);
		return;
	}

	/* LOCAL or CLOSURE — build result object. */
	{
		duk_idx_t obj_idx = duk_push_bare_object(ctx);

		/* Delayed env: activation registers are the local scope. */
		if (env == NULL && func != NULL && DUK_HOBJECT_IS_COMPFUNC(func)) {
			duk_hobject *varmap = duk_hobject_get_varmap(thr, func);

			if (varmap != NULL) {
				if (type == DUK_SCOPE_LOCAL) {
					duk__scope_add_act_vars(thr, act, varmap, obj_idx);
					return; /* result object on stack */
				}
			}
			/* Local scope consumed (even if varmap was stripped). */
			is_first_decenv = 0;

			env = DUK_HCOMPFUNC_GET_LEXENV(thr->heap, (duk_hcompfunc *) func);
			if (env == NULL) {
				env = thr->builtins[DUK_BIDX_GLOBAL_ENV];
			}
		}

		/* Walk the chain, collecting matching scopes. */
		sanity = DUK_HOBJECT_PROTOTYPE_CHAIN_SANITY;
		while (env != NULL && sanity-- > 0) {
			duk_small_uint_t cl = DUK_HOBJECT_GET_CLASS_NUMBER(env);
			int scope_id = duk__scope_classify(thr, env, is_first_decenv);

			if (cl == DUK_HOBJECT_CLASS_DECENV && scope_id == type) {
				duk_hdecenv *decenv = (duk_hdecenv *) env;
				duk_bool_t skip = (type == DUK_SCOPE_CLOSURE); /* inner wins */

				if (decenv->thread != NULL && decenv->varmap != NULL) {
					duk__scope_add_open_vars(thr, decenv, obj_idx, skip);
				} else {
					duk__scope_add_closed_vars(thr, env, obj_idx, skip);
				}

				/* For LOCAL, the first declarative env is the only one. */
				if (type == DUK_SCOPE_LOCAL) {
					return;
				}
			}

			if (cl == DUK_HOBJECT_CLASS_DECENV && is_first_decenv) {
				is_first_decenv = 0;
			}

			env = DUK_HOBJECT_GET_PROTOTYPE(thr->heap, env);
		}

		/* Result object (possibly empty) on stack. */
	}
}

/*
 *  duk_rp_localize(): Copy enumerable own properties of the object at
 *  'obj_idx' into the declarative environment record of the caller at
 *  'call_stack_level'.
 *
 *  This makes the properties visible as if they were local variables
 *  for any name that is NOT already register-bound (i.e. not declared
 *  with 'var' in that function).  GETVAR checks env object properties
 *  after checking registers, so injected names are found on the first
 *  scope lookup.
 *
 *  No scope chain modification — the existing env object is reused.
 *  No changes to duktape.c assertions or unwind logic.
 */

DUK_EXTERNAL void duk_rp_localize(duk_context *ctx, duk_int_t call_stack_level,
                                  duk_idx_t obj_idx, duk_idx_t filter_arr_idx,
                                  duk_bool_t ignore_conflicts) {
	duk_hthread *thr = (duk_hthread *) ctx;
	duk_activation *act;
	duk_hobject *func;
	duk_hobject *env;
	duk_hobject *varmap = NULL;
	duk_idx_t enum_idx;
	duk_idx_t env_stk_idx;

	DUK_ASSERT_API_ENTRY(thr);

	obj_idx = duk_require_normalize_index(ctx, obj_idx);
	if (duk_is_array(ctx, filter_arr_idx)) {
		filter_arr_idx = duk_require_normalize_index(ctx, filter_arr_idx);
	} else {
		filter_arr_idx = -1; /* sentinel: no filter */
	}

	act = duk_hthread_get_activation_for_level(thr, call_stack_level);
	if (act == NULL) {
		return;
	}

	func = DUK_ACT_GET_FUNC(act);

	/* Force environment initialization if delayed. */
	if (act->lex_env == NULL && func != NULL &&
	    DUK_HOBJECT_IS_COMPFUNC(func) && DUK_HOBJECT_HAS_NEWENV(func)) {
		duk_js_init_activation_environment_records_delayed(thr, act);
	}

	env = act->lex_env;
	if (env != NULL && DUK_HOBJECT_IS_DECENV(env)) {
		/* Inside a function — inject into the declarative env. */
		duk_push_hobject(thr, env);
		/* Get the varmap for conflict checking. */
		if (func != NULL && DUK_HOBJECT_IS_COMPFUNC(func)) {
			varmap = duk_hobject_get_varmap(thr, func);
		}
	} else if (env != NULL && env == thr->builtins[DUK_BIDX_GLOBAL_ENV]) {
		/* Global scope — inject into the global object.
		 * No varmap conflict checking needed. */
		duk_push_hobject(thr, ((duk_hobjenv *) env)->target);
	} else {
		return; /* nothing we can inject into (e.g. with block) */
	}
	env_stk_idx = duk_get_top_index(ctx);

	if (filter_arr_idx >= 0) {
		/* Filtered: iterate the array of key names. */
		duk_enum(ctx, filter_arr_idx, DUK_ENUM_ARRAY_INDICES_ONLY | DUK_ENUM_NO_PROXY_BEHAVIOR);
		enum_idx = duk_get_top_index(ctx);

		while (duk_next(ctx, enum_idx, 1 /*get_value*/)) {
			/* stack: [ ... env enum arr_index key_name ] */
			const char *pname = duk_get_string(ctx, -1);
			if (pname != NULL) {
				if (varmap != NULL) {
					duk_hstring *h_key = duk_known_hstring(thr, -1);
					if (duk_hobject_find_entry_tval_ptr(thr->heap, varmap, h_key) != NULL) {
						if (!ignore_conflicts) {
							DUK_ERROR_FMT1(thr, DUK_ERR_TYPE_ERROR,
							    "rampart.localize: '%s' conflicts with a local variable declaration",
							    pname);
						}
						duk_pop_2(ctx);
						continue;
					}
				}
				duk_get_prop_string(ctx, obj_idx, pname);
				/* stack: [ ... env enum arr_index key_name value ] */
				duk_put_prop_string(ctx, env_stk_idx, pname);
			}
			duk_pop_2(ctx); /* pop arr_index + key_name */
		}
	} else {
		/* Unfiltered: copy all enumerable own properties. */
		duk_enum(ctx, obj_idx, DUK_ENUM_OWN_PROPERTIES_ONLY);
		enum_idx = duk_get_top_index(ctx);

		while (duk_next(ctx, enum_idx, 1 /*get_value*/)) {
			/* stack: [ ... env enum key value ] */
			if (varmap != NULL) {
				duk_hstring *h_key = duk_known_hstring(thr, -2);
				if (duk_hobject_find_entry_tval_ptr(thr->heap, varmap, h_key) != NULL) {
					if (!ignore_conflicts) {
						const char *pname = duk_get_string(ctx, -2);
						DUK_ERROR_FMT1(thr, DUK_ERR_TYPE_ERROR,
						    "rampart.localize: '%s' conflicts with a local variable declaration",
						    pname);
					}
					duk_pop_2(ctx);
					continue;
				}
			}
			duk_put_prop(ctx, env_stk_idx); /* env[key] = value; pops key+value */
		}
	}

	duk_pop_2(ctx); /* pop enum + env */
}
