#include "txcoreconfig.h"
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "dbquery.h"
#include "texint.h"
#include "sregex.h"
#include "http.h"

#include "jansson.h"

#define TMPBUFSZ 100

#if defined(_MSC_VER) && _MSC_VER < 1900
#define snprintf _snprintf
#endif

size_t TXjsonFlags = JSON_COMPACT;

/**
 * implement SQL ISJSON(JSON VARCHAR)
 *
 * Returns 1 if valid JSON, 0 if not
 *
 * @param f1 Field containing the string to evaluate
 * @return 0 if successful, FOP_EINVAL if error.
**/

int
txfunc_isjson(FLD *f1)
{
   char                  *a;
   size_t                an;
   ft_long               *val;
   TXPMBUF               *pmbuf = TXPMBUFPN;
   json_t *j;
   json_error_t e;

   if (f1 == FLDPN ||
      (f1->type & DDTYPEBITS) != FTN_CHAR ||
      (a = (char *)getfld(f1, &an)) == CHARPN)
      return(FOP_EINVAL);
   val = (ft_long *)TXcalloc(pmbuf, __FUNCTION__, 2, sizeof(ft_long));
   if (!val) return(FOP_ENOMEM);
   j = json_loads(a, 0, &e);
   if(j)
      *val = (ft_long) 1;
   else
      *val = (ft_long) 0;
   json_decref(j);
   j = NULL;
   f1->type = ((f1->type & ~(DDTYPEBITS | DDVARBIT)) | FTN_LONG);
   f1->kind = TX_FLD_NORMAL;
   f1->elsz = sizeof(ft_long);
   setfld(f1, val, 1);
   f1->n = 1;
   return(0);
}

static char *
TXjsonValueAlloced(json_t *j, int wantlength)
{
   char *res = NULL;
#ifndef __GNUC__
   char tmpbuf[TMPBUFSZ]; /* TmpBuf for printing numbers */
   size_t printed;
#else
   int rc;
#endif

   if(j)
   {
      switch(json_typeof(j)) {
         case JSON_STRING:
            res = strdup(json_string_value(j));
            break;
         case JSON_INTEGER:
#ifdef __GNUC__
            rc = asprintf(&res, "%" JSON_INTEGER_FORMAT, json_integer_value(j));
            if(rc == -1)
            {
              res = NULL;
            }
#else
            printed = snprintf(tmpbuf, TMPBUFSZ, "%" JSON_INTEGER_FORMAT, json_integer_value(j));
            if(printed < (TMPBUFSZ-1))
               res = strdup(tmpbuf);
            else
               res = NULL;
#endif
            break;
         case JSON_REAL:
#ifdef __GNUC__
            rc = asprintf(&res, "%f", json_real_value(j));
            if(rc == -1) res = NULL;
#else
            printed = snprintf(tmpbuf, TMPBUFSZ, "%f", json_real_value(j));
            if(printed < (TMPBUFSZ-1))
               res = strdup(tmpbuf);
            else
               res = NULL;
#endif
            break;
         case JSON_TRUE:
            res = strdup("true");
            break;
         case JSON_FALSE:
            res = strdup("false");
            break;
         case JSON_ARRAY:
            if(wantlength)
            {
#ifdef __GNUC__
               rc = asprintf(&res, "%ld", json_array_size(j));
               if(rc == -1) res = NULL;
#else
               printed = snprintf(tmpbuf, TMPBUFSZ, "%ld", json_array_size(j));
               if(printed < (TMPBUFSZ-1))
                  res = strdup(tmpbuf);
               else
                  res = NULL;
#endif
            }
         case JSON_NULL:
         case JSON_OBJECT:
            break;
         default:
            break;
      }
   }
   return res;
}

json_t *
TXjsonPath(json_t *j, char *path, char **unfoundpath)
{
   char *key, *p, *e;
   int endofkey = 0;
   size_t keylen = 0;
   json_t *next_j;
   long index;

   if(!path)
   {
     putmsg(MERR, NULL, "Null JSON Path");
     return NULL;
   }
   switch (*path) {
      case '$': return TXjsonPath(j, path+1, unfoundpath);
      case '\0': return j;
      case '.':
         p = path+1;
         if (p[0] == '\"')
         {
            e = p;
            while(!endofkey)
            {
               e++;
               switch(*e){
                  case '\"':
                     e++;
                  case '\0':
                     endofkey = 1;
                     break;
                  default:
                     keylen++;
               }
            }
            key = TXcalloc(TXPMBUFPN, __FUNCTION__, keylen+1, sizeof(char));
            strncpy(key, path+2, keylen);
         }
         else
         {
            e = path;
            while(!endofkey)
            {
               e++;
               switch(*e)
               {
                  case '\0':
                  case ' ':
                  case '.':
                  case '[':
                  case ':':
                     endofkey = 1;
                     break;
                  default:
                     if(isspace(*e)) {
                        endofkey = 1;
                     }
                     else {
                        keylen++;
                     }
               }
            }
            key = TXcalloc(TXPMBUFPN, __FUNCTION__, keylen+1, sizeof(char));
            strncpy(key, path+1, keylen);
         }
         next_j = json_object_get(j, key);
         key = TXfree(key);
         if(next_j) {
            return TXjsonPath(next_j, e, unfoundpath);
         }
         else {
            if(unfoundpath) *unfoundpath = path;
            return NULL;
         }
      case '[':
         p = path+1;
         index=strtol(p, &e, 10);
         while(*e && *e != ']') e++;
         e++;
         next_j = json_array_get(j, index);
         if(next_j) {
            return TXjsonPath(next_j, e, unfoundpath);
         }
         else {
            if(unfoundpath) *unfoundpath = path;
            return NULL;
         }

      default:
         putmsg(MERR, NULL, "Invalid JSON Path");
         return NULL;
   }
}

static int
TXjsonPathParent(json_t *j, char *path, json_t **parent, char **childkey)
{
   char *key, *p, *e;
   int endofkey = 0;
   size_t keylen = 0;
   json_t *next_j;
   long index;

/*
printf("pp: %p %s %p %p\n", j, path, parent, childkey);
*/
   switch (*path) {
      case '$': return TXjsonPathParent(j, path+1, parent, childkey);
      case '\0': *parent = NULL; *childkey = NULL; return 0;
      case '.':
         p = path+1;
         if (p[0] == '\"')
         {
            e = p;
            while(!endofkey)
            {
               e++;
               switch(*e){
                  case '\0':
                  case '\"':
                     endofkey = 1;
                     break;
                  default:
                     keylen++;
               }
            }
            key = calloc(keylen+1, sizeof(char));
            strncpy(key, path+2, keylen);
         }
         else
         {
            e = path;
            while(!endofkey)
            {
               e++;
               switch(*e)
               {
                  case '\0':
                  case ' ':
                  case '.':
                  case '[':
                  case ':':
                     endofkey = 1;
                     break;
                  default:
                     if(isspace(*e)) {
                        endofkey = 1;
                     }
                     else {
                        keylen++;
                     }
               }
            }
            key = calloc(keylen+1, sizeof(char));
            strncpy(key, path+1, keylen);
            next_j = json_object_get(j, key);
            if(next_j) {
               if (TXjsonPathParent(next_j, e, parent, childkey) == 0)
               {
                  if(*childkey == NULL) {
                     *childkey = key;
                     *parent = j;
                  } else {
                     if(*parent == NULL) *parent = j;
                     key  = TXfree(key);
                  }
/*
printf("f0: %p %s %p %s %s\n", j, path, *parent, *childkey, key);
*/
                  return 0;
               }
               else
               {
                  key  = TXfree(key);
                  return -1;
               }
            }
            else {
               if(strcmp(path+1, key) == 0)
               {
                  *childkey = key;
                  *parent = j;
/*
printf("f3: %p %s %s %p %p\n", j, path, key, parent, *childkey);
*/
                  return 0;
               }
/*
printf("f2: %p %s %s %p %p\n", j, path, key, parent, *childkey);
*/
               key  = TXfree(key);
               return -1;
            }
         }
      case '[':
         p = path+1;
         index=strtol(p, &e, 10);
         while(*e != ']') e++;
         e++;
         next_j = json_array_get(j, index);
         if(next_j) {
            return TXjsonPathParent(next_j, e, parent, childkey);
         }
         else {
            return -1;
         }

      default:
         putmsg(MERR, NULL, "Invalid JSON Path");
         return -1;
   }
}

/*
 * Traverse a JSON object looking for dpath items to add to a strlst
 *
 * Will need a compare function to compare current item path against
 * the desired path.  The compare function needs to return the following
 * possible returns:
 *
 * No Match       - prune the tree here.  This is a definite NO
 * Possible Match - Keep going down the tree.
 * Leaf Match     - Add the current value to the strlst
 */

static int
TXjsonTraverse(json_t *j, char *cpath, char *dpathre, char *pref, size_t preflen, HTBUF *htbuf)
{
   const char *key;
   json_t *child;
   char *npath;
   size_t index, nplen;
#ifndef __GNUC__
   char tmpbuf[TMPBUFSZ]; /* TmpBuf for printing numbers */
   size_t printed;
#else
   int rc;
#endif

   switch(json_typeof(j))
   {
      case JSON_OBJECT:
         json_object_foreach(j, key, child)
         {
            npath = TXstrcat3(cpath, ".", key);
            nplen = strlen(npath);
            nplen = TX_MIN(nplen, preflen);
            if(strncmp(pref, npath, nplen)) {
               /*
               printf("PRUNE: %s %d\n", npath, strncmp(pref, npath, nplen));
               */
            } else {
               TXjsonTraverse(child, npath, dpathre, pref, preflen, htbuf);
            }
            npath = TXfree(npath);
         }
         return 0;
      case JSON_ARRAY:
         json_array_foreach(j, index, child)
         {
#ifdef __GNUC__
            rc = asprintf(&npath, "%s[%ld]", cpath, index);
            if(rc == -1) npath = NULL;
#else
            printed = snprintf(tmpbuf, TMPBUFSZ, "%ld", index);
            if(printed < (TMPBUFSZ-1))
               npath = TXstrcat4(cpath, "[", tmpbuf, "]");
            else
               npath = NULL;
#endif
            if(npath) {
               nplen = strlen(npath);
               nplen = TX_MIN(nplen, preflen);
               if(strncmp(pref, npath, nplen)) {
                  /*
                  printf("PRUNE: %s %d\n", npath, strncmp(pref, npath, nplen));
                  */
               } else {
                  TXjsonTraverse(child, npath, dpathre, pref, preflen, htbuf);
               }
               npath = TXfree(npath);
            }
         }
         return 0;
      default:
         npath = sregex(dpathre, cpath);
         if(npath && (*npath == '\0')) {
            char *value;

            value = TXjsonValueAlloced(j, 0);
            TXstrlstBufAddString(htbuf, value, -1);
            TXfree(value);
         } else {
         }
         return 0;
   }
}

/**
 *  JSON_VALUE(JSON VARCHAR, PATH VARCHAR)
 *
 * Returns VARCHAR representation of JSON SCALAR referenced by PATH
 *
 */

int
txfunc_json_value(FLD *f1, FLD *f2)
{
   char *jsons, *path;
   char *res = NULL;
   size_t jsons_n, path_n;
   json_t *j, *jres;
   json_error_t e;
   int wantlength = 0;
   size_t pathlen;

   if    (f1 == FLDPN
      || (f1->type & DDTYPEBITS) != FTN_CHAR
      || (jsons = (char *)getfld(f1, &jsons_n)) == CHARPN)
      return(FOP_EINVAL);
   if    (f2 == FLDPN
      || (f2->type & DDTYPEBITS) != FTN_CHAR
      || (path = (char *)getfld(f2, &path_n)) == CHARPN)
      return(FOP_EINVAL);
   j = json_loads(jsons, 0, &e);
   if(!j)
      return(FOP_EINVAL);
   pathlen = strlen(path);
   if((pathlen > 7) && (strcmp(path+(pathlen-7), ".length") == 0))
   {
      path[pathlen-7] = '\0';
      wantlength++;
   }
   jres = TXjsonPath(j, path, NULL);
   res = TXjsonValueAlloced(jres, wantlength);
   json_decref(j);
   j = NULL;
   if(!res) res = strdup("");
   f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
   f1->elsz = 1;
   if(res)
      setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
   else
      setfldandsize(f1, res, 0, FLD_FORCE_NORMAL);
   return 0;
}

/**
 * JSON_QUERY(JSON VARCHAR, PATH VARCHAR)
 *
 * Returns VARCHAR representation of JSON OBJECT referenced by PATH
 *
 */

int
txfunc_json_query(FLD *f1, FLD *f2)
{
   char *jsons, *path;
   char *res = NULL;
   size_t jsons_n, path_n;
   json_t *j, *jres;
   json_error_t e;

   if    (f1 == FLDPN
      || (f1->type & DDTYPEBITS) != FTN_CHAR
      || (jsons = (char *)getfld(f1, &jsons_n)) == CHARPN)
      return(FOP_EINVAL);
   if    (f2 == FLDPN
      || (f2->type & DDTYPEBITS) != FTN_CHAR
      || (path = (char *)getfld(f2, &path_n)) == CHARPN)
      return(FOP_EINVAL);
   j = json_loads(jsons, 0, &e);
   if(!j)
      return(FOP_EINVAL);
   jres = TXjsonPath(j, path, NULL);
   if(jres)
   {
      switch(json_typeof(jres)) {
         case JSON_STRING:
         case JSON_INTEGER:
         case JSON_REAL:
         case JSON_TRUE:
         case JSON_FALSE:
         case JSON_NULL:
            break;
         case JSON_OBJECT:
         case JSON_ARRAY:
            res = json_dumps(jres, TXjsonFlags);
            break;
         default:
            break;
      }
   }
   json_decref(j);
   j = NULL;
   if(!res) res = strdup("");
   f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
   f1->elsz = 1;
   setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
   return 0;
}

/*************************************************************************/
/**
 *
  fldToJson takes a Texis FLD and returns a JSON Object.

  Will also return literal or special values, e.g. string or integer or null

  A string will attempt to be loaded as a valid JSON value, and if that
  fails a JSON String will be created containing that value.

  For example foo and "foo" will both return a JSON string value containing
  foo, and will be output from json functions as "foo".
**/

static int
fldToJson(FLD *f1, json_t **jres)
{
   char *strval;
   FLD *ft, *fr;
   FLDOP *fo;
   json_t *j;
   json_error_t e;
   ft_int64 l;
   ft_double d;

   if(TXftnIsIntegral(f1->type)) {
      if(f1->type == FTN_INT64) {
         l = *(ft_int64 *)getfld(f1, NULL);
      } else {
         if(!(ft = createfld("int64", 1, 0))) return -1;
         fo = TXgetFldopFromCache();
         fopush(fo, f1);
         fopush(fo, ft);
         foop(fo, FOP_CNV);
         fr = fopop(fo);
         l = *(ft_int64 *)getfld(fr, NULL);
         fr = closefld(fr);
         ft = closefld(ft);
         TXreleaseFldopToCache(fo);
      }
      j = json_integer(l);
      if(j) {
         *jres = j;
         return 0;
      }
   } else if(TXftnIsFloatingPoint(f1->type)) {
      if(f1->type == FTN_DOUBLE) {
         d = *(ft_double *)getfld(f1, NULL);
      } else {
         if(!(ft = createfld("double", 1, 0))) return -1;
         fo = TXgetFldopFromCache();
         fopush(fo, f1);
         fopush(fo, ft);
         foop(fo, FOP_CNV);
         fr = fopop(fo);
         d = *(ft_double *)getfld(fr, NULL);
         fr = closefld(fr);
         ft = closefld(ft);
         TXreleaseFldopToCache(fo);
      }
      j = json_real(d);
      if(j) {
         *jres = j;
         return 0;
      }
   }
   if((f1->type & DDTYPEBITS) == FTN_CHAR)
   {
      strval=(char *)getfld(f1, NULL);
   } else {
         if(!(ft = createfld("varchar", 1, 0))) return -1;
         putfld(ft, "", 0);
         fo = TXgetFldopFromCache();
         fopush(fo, f1);
         fopush(fo, ft);
         foop(fo, FOP_CNV);
         fr = fopop(fo);
         strval = (ft_char *)getfld(fr, NULL);
         fr = closefld(fr);
         ft = closefld(ft);
         TXreleaseFldopToCache(fo);
   }
   j = json_loads(strval, 0, &e);
   if(j) {
      *jres = j;
      return 0;
   } else {
      j = json_loads(strval, JSON_DECODE_ANY, &e);
      if(!j)
        j = json_string(strval);
      *jres = j;
      return 0;
   }
}

/*************************************************************************/
/**
 * JSON_MODIFY(JSON VARCHAR, PATH VARCHAR, VALUE)
 *
 * Replace or add the key PATH in JSON with VALUE.  If PATH begins
 * with append then PATH must identify an array, and the value is
 * appended to the array.
 *
 * @param f1 JSON Object to modify
 * @param f2 PATH to identify the key to modify
 * @param f3 VALUE to put at PATH
 * @return VARCHAR representation of JSON OBJECT modified by setting PATH to VALUE
 *
 */


int
txfunc_json_modify(FLD *f1, FLD *f2, FLD *f3)
{
   char *jsons, *path, *childkey = NULL;
   char firsteight[8];
   char *res = NULL;
   size_t jsons_n, path_n;
   json_t *j, *jres = NULL, *jparent = NULL;
   json_error_t e;
   int rc, isappend = 0;

   if    (f1 == FLDPN
      || (f1->type & DDTYPEBITS) != FTN_CHAR
      || (jsons = (char *)getfld(f1, &jsons_n)) == CHARPN)
      return(FOP_EINVAL);
   if    (f2 == FLDPN
      || (f2->type & DDTYPEBITS) != FTN_CHAR
      || (path = (char *)getfld(f2, &path_n)) == CHARPN)
      return(FOP_EINVAL);
   if    (f3 == FLDPN)
      return(FOP_EINVAL);
   j = json_loads(jsons, 0, &e);
   if(!j)
      return(FOP_EINVAL);

   if(path_n > 8)
   {
      strncpy(firsteight, path, 7);
      firsteight[7] = '\0';
      strlwr(firsteight);
      if(!strncmp(firsteight, "append ", 7)) {
         isappend++;
         path += 7;
      }
   }
   while(*path && isspace(*path))
   {
      path++;
   }
   if(isappend)
   {
      jparent = TXjsonPath(j, path, NULL);
      rc = 0;
   }
   else
      rc = TXjsonPathParent(j, path, &jparent, &childkey);
   /*
   printf("PARENT (%s): %d %s\n", childkey, json_typeof(jparent),json_dumps(jparent, TXjsonFlags));
   */
   if(jparent) {
      fldToJson(f3, &jres);
      switch(json_typeof(jparent)) {
         case JSON_STRING:
         case JSON_INTEGER:
         case JSON_REAL:
         case JSON_TRUE:
         case JSON_FALSE:
         case JSON_NULL:
            break;
         case JSON_OBJECT:
            if(isappend)
               rc = -1;
            else {
               json_object_set_new(jparent, childkey, jres);
               childkey = TXfree(childkey);
               res = json_dumps(j, TXjsonFlags);
            }
         case JSON_ARRAY:
            if(isappend) {
               json_array_append_new(jparent, jres);
               res = json_dumps(j, TXjsonFlags);
            }
            break;
         default:
            break;
      }
   } else {
      rc = -1;
   }
   json_decref(j);
   j = NULL;
   if(!res) res = strdup("");
   f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
   f1->elsz = 1;
   setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
   return rc;
}

/*************************************************************************/
/**
 * JSON_MERGE_PATCH(Target, Patch)
 *
 * Sets Target to value after using Patch
 *
 * Patch will be all used up, should not be freed after
 * @param Target the target document
 * @param Patch the patch to apply to the documet
 * @param nTarget the patched Target is set here
 * @param e an error object
 * @return 0 on success
 */
static int
json_merge_patch(json_t *Target, json_t *Patch, json_t **nTarget, json_error_t *e)
{
  const char *key;
  json_t *targetValue, *patchValue;

  /* If Patch is not an object, return it */
  if(json_typeof(Patch) != JSON_OBJECT)
  {
    *nTarget = Patch;
    json_incref(Patch);
    return 0;
  }
  /* IF target isn't an object, ignore it, and start fresh */
  if(!Target || json_typeof(Target) != JSON_OBJECT)
  {
    Target = json_object();
  }
  *nTarget = Target;
  /* For each key in the patch */
  json_object_foreach(Patch, key, patchValue)
  {
    /* NULL means delete */
    if(json_typeof(patchValue) == JSON_NULL)
    {
      json_object_del(Target, key);
    }
    else
    {
      json_t *newValue;

      targetValue = json_object_get(Target, key);
      json_merge_patch(targetValue, patchValue, &newValue, e);
      if(targetValue != newValue)
      {
        json_object_set_new(Target, key, newValue);
      }
    }
  }
  return 0;
}

/*************************************************************************/
/**
 * JSON_MERGE_PATCH(Target, Patch)
 *
 * Sets Target to value after using Patch
 *
 * Patch will be all used up, should not be freed after
 * @param f1 the target document
 * @param f2 the patch to apply to the documet
 * @return VARCHAR representation of mergered document
 */
int
txfunc_json_merge_patch(FLD *f1, FLD *f2)
{
  char *res = NULL;
  json_t *OriginalJson, *PatchJson, *resultJson;
  json_error_t e;
  int rc = 0;

  fldToJson(f1, &OriginalJson);
  fldToJson(f2, &PatchJson);

  if(!OriginalJson)
    return FOP_EINVAL;
  json_merge_patch(OriginalJson, PatchJson, &resultJson, &e);
  res = json_dumps(resultJson, JSON_COMPACT | JSON_ENCODE_ANY);
  if(resultJson != OriginalJson)
  {
    json_decref(resultJson);
  }
  json_decref(OriginalJson);
  json_decref(PatchJson);

  if(!res) res = strdup("");
  f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
  f1->elsz = 1;
  setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
  return rc;
}

/*************************************************************************/

static int
json_merge_preserve(json_t *Target, json_t *Patch, json_t **nTarget, json_error_t *e)
{
  const char *key;
  json_t *targetValue, *patchValue;
  json_type targetType, patchType;
  json_t *NewArray = NULL;

  *nTarget = Target;
  targetType = json_typeof(Target);
  patchType = json_typeof(Patch);
  (void)e;

  if (targetType == JSON_OBJECT && patchType == JSON_OBJECT)
  {
    /* For each key in Target, merge the corresponding Patch */
    /* For each key in the patch */
    json_object_foreach(Patch, key, patchValue)
    {
      /* NULL means delete */
      if(json_typeof(patchValue) == JSON_NULL)
      {
        json_object_del(Target, key);
      }
      else
      {
        json_t *newTargetV;

        targetValue = json_object_get(Target, key);
        if(targetValue)
        {
          json_merge_preserve(targetValue, patchValue, &newTargetV, e);
          if(targetValue != newTargetV)
          {
            json_object_set_new(Target, key, newTargetV);
          }
        } else {
          json_object_set(Target, key, patchValue);
        }
      }
    }
    return 0;
  }
  if (targetType != JSON_ARRAY)
  {
    NewArray = json_array();
    json_array_append(NewArray, Target);
    *nTarget = Target = NewArray;
    NewArray = NULL;
    targetType = JSON_ARRAY;
  }
  if (patchType != JSON_ARRAY)
  {
    NewArray = json_array();
    json_array_append(NewArray, Patch);
    Patch = NewArray;
    patchType = JSON_ARRAY;
  }
  if (targetType == JSON_ARRAY && patchType == JSON_ARRAY)
  {
    json_array_extend(Target, Patch);
    if(NewArray)
    {
      json_decref(NewArray);
    }
    return 0;
  }
  return -1;
}

int
txfunc_json_merge_preserve(FLD *f1, FLD *f2)
{
  char *res = NULL;
  json_t *OriginalJson, *PatchJson, *resultJson;
  json_error_t e;
  int rc = 0;

  fldToJson(f1, &OriginalJson);
  fldToJson(f2, &PatchJson);

  if(!OriginalJson)
    return FOP_EINVAL;
  json_merge_preserve(OriginalJson, PatchJson, &resultJson, &e);
  res = json_dumps(resultJson, JSON_COMPACT | JSON_ENCODE_ANY);
  if(OriginalJson != resultJson)
  {
    json_decref(resultJson);
  }
  json_decref(OriginalJson);
  json_decref(PatchJson);

  if(!res) res = strdup("");
  f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
  f1->elsz = 1;
  setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
  return rc;
}

/*************************************************************************/
/**
 * Return the JSON type of an object.
**/

int
txfunc_json_type(FLD *f1)
{
  char *res = NULL;
  json_t *Json;
  int rc = 0;

  fldToJson(f1, &Json);
  if(!Json)
    return FOP_EINVAL;
  switch(json_typeof(Json))
  {
    case JSON_STRING:
      res = strdup("STRING");
      break;
    case JSON_INTEGER:
      res = strdup("INTEGER");
      break;
    case JSON_REAL:
      res = strdup("DOUBLE");
      break;
    case JSON_TRUE:
    case JSON_FALSE:
      res = strdup("BOOLEAN");
      break;
    case JSON_NULL:
      res = strdup("NULL");
      break;
    case JSON_OBJECT:
      res = strdup("OBJECT");
      break;
    case JSON_ARRAY:
      res = strdup("ARRAY");
      break;
  }
  json_decref(Json);
  if(!res) res = strdup("");
  f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
  f1->elsz = 1;
  setfldandsize(f1, res, strlen(res) + 1, FLD_KEEP_KIND);
  return rc;
}

/*************************************************************************/

/*
 * #define JSON_MAX_INDENT         0x1F
 * #define JSON_INDENT(n)          ((n) & JSON_MAX_INDENT)
 * #define JSON_COMPACT            0x20
 * #define JSON_ENSURE_ASCII       0x40
 * #define JSON_SORT_KEYS          0x80
 * #define JSON_PRESERVE_ORDER     0x100
 * #define JSON_ENCODE_ANY         0x200
 * #define JSON_ESCAPE_SLASH       0x400
 * #define JSON_REAL_PRECISION(n)  (((n) & 0x1F) << 11)
 * #define JSON_EMBED              0x10000
 *
 */

static int
parsejsonfmt(char *prop)
{
  size_t indent = 1; /* Default Indent Level if INDENT specified with no value */
  char *flag, *lprop = NULL;
#ifdef _WIN32
  /* Windows has no strtok_r().  MSDN docs say MS strtok() uses
   * thread-local storage, so ok to use?
   */
#  define STRTOK(a, b)  strtok(a, b)
#else /* !_WIN32 */
  char  *saveptr = NULL;
#  define STRTOK(a, b)  strtok_r(a, b, &saveptr)
#endif /* !_WIN32 */
  int TXjsonFlags;

/* Dup and lower first.  Lets strtok write to string, and no repeated dups */
  lprop = TXstrdup(NULL, __FUNCTION__, prop);
  if(!lprop) return -1;
  strlwr(lprop);
  flag = STRTOK(lprop, " ,");
  TXjsonFlags = 0; /* Start with no flags, add flags */
  while (flag)
  {
          if(!strcmp(flag, "compact")) TXjsonFlags |= JSON_COMPACT;
     else if(!strcmp(flag, "ensure_ascii")) TXjsonFlags |= JSON_ENSURE_ASCII;
     else if(!strcmp(flag, "sort_keys")) TXjsonFlags |= JSON_SORT_KEYS;
     else if(!strcmp(flag, "preserve_order")) TXjsonFlags |= JSON_PRESERVE_ORDER; /* Deprecated/default */
     else if(!strcmp(flag, "encode_any")) TXjsonFlags |= JSON_ENCODE_ANY;
     else if(!strcmp(flag, "escape_slash")) TXjsonFlags |= JSON_ESCAPE_SLASH;
     else if(!strcmp(flag, "embed")) TXjsonFlags |= JSON_EMBED;
     else if(!strcmp(flag, "indent")) TXjsonFlags |= JSON_INDENT(indent);
     else if(!strncmp(flag, "indent(", 7))
          {
              indent = strtol(flag+7,NULL, 10);
              TXjsonFlags |= JSON_INDENT(indent);
          }
     flag = STRTOK(NULL, " ,");
  }
  TXfree(lprop);
  return TXjsonFlags;
#undef STRTOK
}

/*************************************************************************/
/**
 * Format the JSON type of an object.
**/

int
txfunc_json_format(FLD *f1, FLD *f2)
{
  char *res = NULL;
  json_t *Json;
  int rc = 0;

  fldToJson(f1, &Json);
  if(!Json)
    return FOP_EINVAL;
  res = json_dumps(Json, parsejsonfmt(getfld(f2, NULL)));
  json_decref(Json);
  if(!res) res = strdup("");
  f1->type = ((f1->type & ~DDTYPEBITS) | FTN_CHAR | DDVARBIT);
  f1->elsz = 1;
  setfldandsize(f1, res, strlen(res) + 1, FLD_FORCE_NORMAL);
  return rc;
}

int
TXsetjsonfmt(char *prop)
{
  TXjsonFlags = parsejsonfmt(prop);
  return 0;
}

/************************************************************************/
static int
TXmkComputedJsonStrlst(FLD *f)
{
   FLD	*curFld = NULL;
   void *v;
   char *js = NULL;
   json_t *j;
   json_error_t e;
#ifndef __GNUC__
   char tmpbuf[TMPBUFSZ]; /* TmpBuf for printing numbers */
   size_t printed;
#endif

   if (!f) return -1; /* Fail */

   switch(TXfldbasetype(f))
   {
      case FTN_STRLST:
         break;
      default:
         putmsg(MERR, __FUNCTION__, "Internal error: Unexpected fld type");
         return -1;
   }
   curFld = f->fldlist[0];
   if (!curFld) return -1;
   v = curFld->v;
   if (!v) return -1;
   switch (TXfldbasetype(curFld))
   {
#ifdef NEEDS_DOING
		case FTN_INDIRECT:
                                          if (!*(char *)v) break;
                                          fh = fopen(v, "rb");
                                          if (!fh) break;
                                          nr = fread(p, 1, avail, fh);
                                          p += nr;
                                          fclose(fh);
                                          fh = NULL;
                                          break;
		case FTN_BLOBI:
			blobi = (ft_blobi *)v;
			v2 = TXblobiGetPayload(blobi, &sz);
			if (v2 && (sz == 1) && (*(char *)v2 == '\0'))
			{
				/* Bug 7593: also do this in first pass */
				TXblobiFreeMem(blobi);
				v2 = NULL;
			}
			if (!v2) break;
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
					   "Internal error: Truncating blob");
					sz = avail;
				}
				memcpy(p, v2, sz);
		zapBlobiNuls:
			/* Eliminate trailing nuls: */
			/* Bug 7593: also do this in first pass */
			np = p + sz - 1;
			while (np >= p)		/* Bug 7593 add `=' */
			{
				if(*np == '\0')
				np--;
				else
				break;
			}
			p = np + 1;
			TXblobiFreeMem(blobi);
			v2 = NULL;
			break;
		case FTN_INTERNAL:
			s = tx_fti_obj2str((ft_internal *)v);
			if (!s) break;
			sz = strlen(s);
			if (accumBuf)
			{
				if (!htbuf_write(accumBuf, s, sz)) goto err;
			}
			else
			{
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
				   "Internal error: Truncating FTN_INTERNAL");
					sz = avail;
				}
				memcpy(p, s, sz);
				p += sz;
			}
			break;
#endif
		case FTN_CHAR:
		case FTN_BYTE:
                     js = v;
                     break;
#ifdef NEEDS_DOING
		default:
			/* Convert to varchar: */
			if (fldOp == FLDOPPN &&
			    (fldOp = dbgetfo()) == FLDOPPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLDOP");
				goto err;
			}
			if (cnvFld == FLDPN &&
			    (cnvFld = createfld("varchar", 1, 0)) == FLDPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLD");
				goto err;
			}
			putfld(cnvFld, "", 0);
			if (fopush(fldOp, curFld) != 0 ||
			    fopush(fldOp, cnvFld) != 0 ||
			    foop(fldOp, FOP_CNV) != 0 ||
			    (resFld = fopop(fldOp)) == FLDPN)
			{
				putmsg(MERR, __FUNCTION__,
	  "Cannot convert field type %s to varchar for virtual field",
				       TXfldtypestr(curFld));
				goto err;
			}
			js = getfld(resFld, &sz);
			resFld = closefld(resFld);
			break;
#else
            default:
                 putmsg(MERR, __FUNCTION__,
                         "Cannot convert field type %s to varchar for JSON field",
                        TXfldtypestr(curFld));
				return -1;
#endif
   }
   if(!js)
      return -1;
   j = json_loads(js, 0, &e);
   if(j)
   {
      char *pathexp;
      char *s, *e;
      char *re = NULL;
      size_t relen = 0;
      int i;
      HTBUF *htbuf;

      if(!(htbuf = openhtbuf())) return -1;
      if(!TXstrlstBufBegin(htbuf)) {
         htbuf = closehtbuf(htbuf);
         return -1;
      }
      pathexp = (char *)getfld(f->fldlist[1], NULL);
      /*
       * Convert pathexp to sregex expression
       *
       * Need to escape:
       * $.item[*].array[3]
       * \$\.item\[[0-9][0-9]*\]\.array\[3\]
       */
      for (i = 0; i < 2; i++)
      {
         if (i == 0) {
            relen = 1;
         } else {
            e = re = calloc(1, relen+1);
            if(!e)
               return -1;
            *re++ = '^';
         }
         for (s = pathexp; *s; s++)
         {
            switch (*s)
            {
               case '$':
               case '.':
               case '[':
               case ']':
                  if (i == 0) {
                     relen += 2;
                  } else {
                     *re++ = '\\';
                     *re++ = *s;
                  }
                  break;
               case '*':
                  if (i == 0) {
                     relen += 11;
                  } else {
                     strcat(re, "[0-9][0-9]*");
                     re += 11;
                  }
                  break;
               default:
                  if (i == 0) {
                     relen += 1;
                  } else {
                     *re++ = *s;
                  }
                  break;
            }
         }
      }

      re = sregcmp(e, '\\');
      sregprefix(re, e, relen, &relen, 0);
      TXjsonTraverse(j, "$", re, e, relen, htbuf);
      TXstrlstBufEnd(htbuf);

      re = TXfree(re);
      e = TXfree(e);
      relen = htbuf_getdata(htbuf, &e, 0x3);
      htbuf = closehtbuf(htbuf);
      if(e)
         setfldandsize(f, e, relen, FLD_KEEP_KIND);
      else
         setfldandsize(f, NULL, 0, FLD_KEEP_KIND);
      json_decref(j);
   } else {
      setfldandsize(f, NULL, 0, FLD_KEEP_KIND);
      return -1;
   }
   return 0;
}

/************************************************************************/
int
TXmkComputedJson(FLD *f)
{
   FLD	*curFld = NULL;
   void *v;
   char *js = NULL;
   json_t *j;
   json_error_t e;
#ifndef __GNUC__
   char tmpbuf[TMPBUFSZ]; /* TmpBuf for printing numbers */
   size_t printed;
#endif

   if (!f) return -1; /* Fail */
   switch(TXfldbasetype(f))
   {
      case FTN_STRLST:
         return TXmkComputedJsonStrlst(f);
      case FTN_CHAR:
         break;
      default:
         putmsg(MERR, __FUNCTION__, "Internal error: Unexpected fld type");
         return -1;
   }

   if(f->vfc == 0) return -1;
   curFld = f->fldlist[0];
   if (!curFld) return -1;
   v = curFld->v;
   if (!v) return -1;
   switch (TXfldbasetype(curFld))
   {
#ifdef NEEDS_DOING
		case FTN_INDIRECT:
                                          if (!*(char *)v) break;
                                          fh = fopen(v, "rb");
                                          if (!fh) break;
                                          nr = fread(p, 1, avail, fh);
                                          p += nr;
                                          fclose(fh);
                                          fh = NULL;
                                          break;
		case FTN_BLOBI:
			blobi = (ft_blobi *)v;
			v2 = TXblobiGetPayload(blobi, &sz);
			if (v2 && (sz == 1) && (*(char *)v2 == '\0'))
			{
				/* Bug 7593: also do this in first pass */
				TXblobiFreeMem(blobi);
				v2 = NULL;
			}
			if (!v2) break;
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
					   "Internal error: Truncating blob");
					sz = avail;
				}
				memcpy(p, v2, sz);
		zapBlobiNuls:
			/* Eliminate trailing nuls: */
			/* Bug 7593: also do this in first pass */
			np = p + sz - 1;
			while (np >= p)		/* Bug 7593 add `=' */
			{
				if(*np == '\0')
				np--;
				else
				break;
			}
			p = np + 1;
			TXblobiFreeMem(blobi);
			v2 = NULL;
			break;
		case FTN_INTERNAL:
			s = tx_fti_obj2str((ft_internal *)v);
			if (!s) break;
			sz = strlen(s);
			if (accumBuf)
			{
				if (!htbuf_write(accumBuf, s, sz)) goto err;
			}
			else
			{
				if (sz > avail)
				{
					putmsg(MERR, __FUNCTION__,
				   "Internal error: Truncating FTN_INTERNAL");
					sz = avail;
				}
				memcpy(p, s, sz);
				p += sz;
			}
			break;
#endif
		case FTN_CHAR:
		case FTN_BYTE:
                     js = v;
                     break;
#ifdef NEEDS_DOING
		default:
			/* Convert to varchar: */
			if (fldOp == FLDOPPN &&
			    (fldOp = dbgetfo()) == FLDOPPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLDOP");
				goto err;
			}
			if (cnvFld == FLDPN &&
			    (cnvFld = createfld("varchar", 1, 0)) == FLDPN)
			{
				putmsg(MERR + MAE, __FUNCTION__,
				       "Cannot open FLD");
				goto err;
			}
			putfld(cnvFld, "", 0);
			if (fopush(fldOp, curFld) != 0 ||
			    fopush(fldOp, cnvFld) != 0 ||
			    foop(fldOp, FOP_CNV) != 0 ||
			    (resFld = fopop(fldOp)) == FLDPN)
			{
				putmsg(MERR, __FUNCTION__,
	  "Cannot convert field type %s to varchar for virtual field",
				       TXfldtypestr(curFld));
				goto err;
			}
			js = getfld(resFld, &sz);
			resFld = closefld(resFld);
			break;
#else
            default:
                 putmsg(MERR, __FUNCTION__,
                         "Cannot convert field type %s to varchar for JSON field",
                        TXfldtypestr(curFld));
				return -1;
#endif
   }
   if(!js)
      return -1;
   j = json_loads(js, 0, &e);
   if(j)
   {
      char *res;
      json_t *jres;
      double *dres=NULL;
      size_t ressz=0;

      if((jres = TXjsonPath(j, getfld(f->fldlist[1], NULL), NULL))) {

         //-AJF 20250101 - need to distinguish between varchar and json data in rampart
         // Hack by adding \xff\xf- to beginning of string to mark json type.  Decoded in rampart-sql.c

         switch(json_typeof(jres)) {
            case JSON_STRING:
               if(TX_is_rampart)
               {
#ifdef __GNUC__
                   if(asprintf(&res, "\xff\xff%s", json_string_value(jres)) == -1) res = NULL;
#else
                   printed = snprintf(tmpbuf, TMPBUFSZ, "\xff\xff%s",json_string_value(jres) );
                   if(printed < (TMPBUFSZ-1))
                      res = strdup(tmpbuf);
                   else
                      res = NULL;
#endif
               }
               else
                   res = strdup(json_string_value(jres));
               ressz=strlen(res)+1;
               break;
            case JSON_INTEGER:
               if(TX_is_rampart)
               {
                   ressz = 2*sizeof(double)+1;
                   dres = (double *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, ressz);
                   res=(char*)dres;
                   dres[1]=(double) json_integer_value(jres);
                   res[0]='\xff';
                   res[1]='\xfe';
               }
               else
               {
#ifdef __GNUC__
                   if(asprintf(&res, "%" JSON_INTEGER_FORMAT, json_integer_value(jres)) == -1) res = NULL;
#else
                   printed = snprintf(tmpbuf, TMPBUFSZ, "%" JSON_INTEGER_FORMAT, json_integer_value(jres));
                   if(printed < (TMPBUFSZ-1))
                      res = strdup(tmpbuf);
                   else
                      res = NULL;
#endif
               }
               break;
            case JSON_REAL:
               if(TX_is_rampart)
               {
                   ressz = 2*sizeof(double)+1;
                   dres = (double *)TXcalloc(TXPMBUFPN, __FUNCTION__, 1, ressz);
                   res=(char*)dres;
                   dres[1]=json_real_value(jres);
                   res[0]='\xff';
                   res[1]='\xfe';
               }
               else
               {
#ifdef __GNUC__
                   if(asprintf(&res, "%f", json_real_value(jres)) == -1) res = NULL;
#else
                   printed = snprintf(tmpbuf, TMPBUFSZ, "%f", json_real_value(jres));
                   if(printed < (TMPBUFSZ-1))
                      res = strdup(tmpbuf);
                   else
                      res = NULL;
#endif
               }
               break;
            case JSON_TRUE:
               if(TX_is_rampart)
               {
                   res = strdup("\xff\xfd");
                   ressz=3;
               }
               else
               {
                   res = strdup("true");
                   ressz=5;
               }
               break;
            case JSON_FALSE:
               if(TX_is_rampart)
               {
                   res = strdup("\xff\xfc");
                   ressz=3;
               }
               else
               {
                   res = strdup("false");
                   ressz=6;
               }
               break;
            case JSON_OBJECT:
            case JSON_ARRAY:
              res = json_dumps(jres, TXjsonFlags);
              if(TX_is_rampart)
              {
                  char *res2;
#ifdef __GNUC__
                   if(asprintf(&res2, "\xff\xfa%s", res) == -1) res = NULL;
#else
                   printed = snprintf(tmpbuf, TMPBUFSZ, "\xff\xfa%s", res);
                   if(printed < (TMPBUFSZ-1))
                   {
                      res2 = strdup(tmpbuf);
                   }
                   else
                      res2 = NULL;
#endif
                  ressz = strlen(res) +3;
                  free(res);
                  res=res2;
              }
              break;
            case JSON_NULL:
               if(TX_is_rampart)
               {
                   res = strdup("\xff\xfb");
                   ressz=3;
               }
               else
               {
                   res = strdup("null");
                   ressz=5;
               }
              break;
            default:
               res = strdup("WTF: Fix mkComputedJson");

         }

         if(res)
         {
            if(!TX_is_rampart)
                ressz=strlen(res)+1;
            setfldandsize(f, res, ressz, FLD_KEEP_KIND);
         }
         else
            setfldandsize(f, res, 0, FLD_KEEP_KIND);
      }
      else {
         setfldandsize(f, NULL, 0, FLD_KEEP_KIND);
      }
      json_decref(j);
   } else {
      setfldandsize(f, NULL, 0, FLD_KEEP_KIND);
      return -1;
   }
   return 0;
}
/************************************************************************/

#ifdef TEST
int
main()
{
   json_t *j;
   json_error_t e;

   j = json_loads("{ \"valid\": true }", 0, &e);
   if(j)
      printf("Success\n");
   else
      printf("Fail\n");
}
#endif
