The rampart-auth module
=======================

Preface
-------

License
~~~~~~~

The rampart-auth module is released under the MIT license.

What does it do?
~~~~~~~~~~~~~~~~

The rampart-auth module provides session-based authentication for the
rampart-server HTTP module.  It is implemented as a C module
(``rampart-auth.so``) that runs entirely in native code to validate
sessions without invoking the JavaScript interpreter.  This means that
authentication checks — including cookie extraction, LMDB session
lookup, expiry checking, and privilege level enforcement — happen at
C speed on every request, with no Duktape overhead.

The module is accompanied by ``auth.js``, a JavaScript module included
in the ``web_server/apps/`` directory.  ``auth.js`` handles the
operations that do not need to be fast: user creation, login, logout,
password management, and an administrative web interface.  These
operations are not in the hot path of normal server functions.

Together, the C module and the JS module provide:

- Session-based authentication with cookie tokens
- Privilege levels for path-based access control
- CSRF protection on state-changing requests
- Sliding session expiry with configurable refresh intervals
- Account lockout after failed login attempts
- Password hashing via ``rampart-crypto``
- A pluggable architecture for custom auth modules
- A CLI administration tool
- An administrative web interface
- OAuth plugin architecture with included Google and Facebook plugins
- Display cookie for showing user info on static pages
- Email-based password reset and verification flows

How does it work?
~~~~~~~~~~~~~~~~~

The ``rampart-auth.so`` C module is loaded by the server when
``authMod`` is set in the server configuration.  On every request, the
C module:

1. Extracts the session cookie from the HTTP headers.
2. Looks up the session in an LMDB database (via the ``rampart-lmdb``
   module's Duktape C API — fast pointer operations, not JS interpretation).
3. Checks the session's expiry timestamp.
4. For file requests: returns ``true`` (serve) or ``false`` (deny).
5. For app module requests: attaches the session data as ``req.userAuth``
   so the JavaScript handler can inspect the authenticated user's
   information.

Path-based access control is enforced by the server itself: paths listed
in the configuration with a required privilege level are checked against
the user's ``authLevel``.  Denied requests receive either a ``403
Forbidden`` or a ``302`` redirect to a login page, depending on the
file type and configuration.

Loading and Using the Module
----------------------------

Enabling Authentication
~~~~~~~~~~~~~~~~~~~~~~~

Authentication is enabled in ``web_server_conf.js`` (or directly in a
``server.start()`` call) by setting two properties:

.. code-block:: javascript

    var serverConf = {
        // ... other config ...

        authMod:     true,
        authModConf: working_directory + '/auth-conf.js',

        // ... rest of config ...
    };

Where:

    * ``authMod`` is a :green:`Boolean`, :green:`String`, or
      :green:`Function`:

      - ``true`` — loads the built-in ``rampart-auth.so`` module.
      - A :green:`String` — loads a custom auth module by name or path.
      - A :green:`Function` — uses an inline auth function (see
        `Custom Auth Modules`_ below).
      - ``false`` or omitted — authentication is disabled.

    * ``authModConf`` is a :green:`String`, the path to a JavaScript
      module that exports the authentication configuration object.

Configuration File
~~~~~~~~~~~~~~~~~~

The configuration is a JavaScript module (not JSON) so that comments
are supported.  It exports an object with the following properties:

.. code-block:: javascript

    // auth-conf.js
    module.exports = {

        // Session cookie name (default: "rp_session")
        cookieName: "rp_session",

        // LMDB database path, relative to server root (default: "data/auth")
        dbPath: "data/auth",

        // CSRF protection on POST/PUT/DELETE/PATCH (default: true)
        csrf: true,

        // Paths exempt from CSRF checking
        csrfExemptPaths: ["/apps/api/webhook/"],

        // File extensions that trigger 302 redirect instead of 403 on
        // denied requests (default: ["", ".html", ".htm", ".txt"])
        redirectExtensions: ["", ".html", ".htm", ".txt"],

        // Sliding session expiry (in seconds)
        sessionExpiry: 86400,       // new expiry on refresh (default: 24h)
        sessionRefresh: 300,        // min interval between refreshes (default: 5min)
        sessionRefreshUrgent: 3600, // always refresh if < this remaining (default: 1h)

        // Revalidation of OPEN websockets (default: 20; 0 = every frame)
        wsRecheckSeconds: 20,

        // Account lockout
        lockoutAttempts: 5,     // max failed attempts before lockout (default: 5)
        lockoutWindow:   300,   // time window in seconds (default: 5 min)
        lockoutDuration: 900,   // lockout duration in seconds (default: 15 min)

        // Password policy
        minPasswordLength: 7,   // minimum password length (default: 7)

        // Cookie flags
        cookieFlags: {
            httpOnly: true,     // not accessible to JavaScript (default: true)
            sameSite: "Lax",    // CSRF protection level (default: "Lax")
            secure:   true,     // only send over HTTPS (default: true)
            path:     "/"       // cookie path (default: "/")
        },

        // Self-registration
        allowRegistration: false,       // enable /apps/auth/register (default: false)
        requireEmailVerification: true, // require email verify before login (default: true)
        siteUrl: "https://example.com", // base URL for links in emails

        // Email configuration (required for verification and password reset emails)
        email: {
            from: "noreply@example.com",
            method: "direct",   // "direct", "relay", "smtp", or "gmailApp"
            // For "smtp":    smtpUrl, user, pass
            // For "gmailApp": user, pass
            // For "relay":    relay, relayPort
        },

        // OAuth providers (optional — enables OAuth plugin loading)
        oauth: {
            google: {
                clientId:     "your-client-id.apps.googleusercontent.com",
                clientSecret: "your-client-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/google/callback"
            },
            facebook: {
                clientId:     "your-app-id",
                clientSecret: "your-app-secret",
                callbackUrl:  "https://example.com/apps/auth/oauth/facebook/callback",
                apiVersion:   "v21.0"  // optional, defaults to "v21.0"
            }
        },

        // Display cookie (optional — client-readable cookie for static pages)
        displayCookie: {
            cookieName: "rp_user",                // cookie name (default: "rp_user")
            fields:     ["name", "picture"]        // session fields to include
        },

        // Protected paths
        protectedPaths: {
            "/admin/": {
                level: 0,
                redirect: "/apps/auth/login?returnTo=$origin"
            },
            "/private/": {
                level: 1,
                redirect: "/apps/auth/login?returnTo=$origin"
            }
        }
    };

Configuration Properties
""""""""""""""""""""""""

    * ``cookieName`` — :green:`String`.  The name of the session cookie
      (default: ``"rp_session"``).

    * ``dbPath`` — :green:`String`.  Path to the LMDB database directory.
      Relative paths are resolved from the server root
      (default: ``"data/auth"``).

    * ``csrf`` — :green:`Boolean`.  Enable CSRF protection.  When ``true``,
      POST, PUT, DELETE, and PATCH requests from authenticated sessions
      are rejected unless a valid CSRF token is provided in the form body
      (as ``_csrf``) or in the ``X-CSRF-Token`` header.  GET and HEAD
      requests are always exempt (default: ``true``).

    * ``csrfExemptPaths`` — :green:`Array` of :green:`Strings`.  Path
      prefixes exempt from CSRF checking.  Useful for webhooks or API
      endpoints that receive external POSTs.

    * ``redirectExtensions`` — :green:`Array` of :green:`Strings`.  File
      extensions that trigger a 302 redirect to the ``redirect`` URL
      instead of a 403 when access is denied.  Other extensions (images,
      CSS, JS, etc.) receive a plain 403.  An empty string (``""``)
      matches requests with no extension
      (default: ``["", ".html", ".htm", ".txt"]``).

    * ``sessionExpiry`` — :green:`Number`.  Session lifetime in seconds.
      When a session is refreshed, its expiry is set to
      ``now + sessionExpiry`` (default: ``86400``).

    * ``sessionRefresh`` — :green:`Number`.  Minimum interval between
      session refreshes in seconds.  A session is written back to LMDB
      only if this many seconds have passed since the last refresh.  Set
      to ``0`` to disable sliding expiry (default: ``300``).

    * ``sessionRefreshUrgent`` — :green:`Number`.  If fewer than this
      many seconds remain before the session expires, refresh immediately
      regardless of ``sessionRefresh`` (default: ``3600``).

    * ``wsRecheckSeconds`` — :green:`Number`.  How often an already-open
      websocket is revalidated, in seconds.  The upgrade itself is always
      authorised against ``protectedPaths`` before the handshake is
      generated; this setting governs only how long a session that is
      revoked or expires *mid-connection* may keep using a socket that is
      already open.  A denied frame closes the socket with a websocket
      close frame (status 1008, policy violation) rather than an HTTP
      response, which a peer in framing mode cannot parse.

      ``0`` revalidates on every frame the client sends — exact, but it
      roughly halves frame throughput, because the whole ``req`` object
      is passed through the auth function each time.  Note that an
      **idle** socket is not revalidated at any setting: nothing runs
      until the client sends data (default: ``20``).

    * ``lockoutAttempts`` — :green:`Number`.  Maximum failed login
      attempts before the account is temporarily locked.  Set to ``0`` to
      disable lockout (default: ``5``).

    * ``lockoutWindow`` — :green:`Number`.  Time window in seconds for
      counting failed attempts (default: ``300``).

    * ``lockoutDuration`` — :green:`Number`.  Duration of account lockout
      in seconds (default: ``900``).

    * ``minPasswordLength`` — :green:`Number`.  Minimum password length
      for user creation and password changes (default: ``7``).

    * ``cookieFlags`` — :green:`Object`.  Flags for the session cookie.
      Properties: ``httpOnly`` (default: ``true``), ``sameSite``
      (default: ``"Lax"``), ``secure`` (default: ``true``), ``path``
      (default: ``"/"``).  The ``secure`` flag defaults to ``true``
      meaning cookies are only sent over HTTPS.  Set to ``false``
      explicitly if running without HTTPS.  The server cannot
      auto-detect HTTPS because it may be behind a reverse proxy.

    * ``allowRegistration`` — :green:`Boolean`.  Enable the
      self-registration form at ``/apps/auth/register``.  When enabled,
      users can create their own accounts.  (default: ``false``).

    * ``requireEmailVerification`` — :green:`Boolean`.  When ``true``,
      users created via self-registration must verify their email
      address before they can log in.  Users created via the admin
      panel or CLI have ``emailVerified: true`` by default and are
      not affected.  (default: ``true``).

    * ``siteUrl`` — :green:`String`.  Base URL of the site (e.g.,
      ``"https://example.com"``), used to construct full URLs in
      verification and password reset emails.  No trailing slash.

    * ``email`` — :green:`Object`.  SMTP configuration for sending
      verification and password reset emails.  If omitted, email-based
      features are disabled.  Uses the ``rampart-email`` module.

      Properties:

      - ``from`` — sender email address (required).
      - ``method`` — delivery method: ``"direct"`` (default),
        ``"relay"``, ``"smtp"``, or ``"gmailApp"``.
      - ``user``, ``pass`` — credentials for ``"smtp"`` and
        ``"gmailApp"`` methods.
      - ``smtpUrl`` — SMTP server URL for the ``"smtp"`` method.
      - ``relay``, ``relayPort`` — relay server for the ``"relay"``
        method.

      A per-username cooldown (60 seconds) prevents abuse of email
      sending endpoints.

    * ``oauth`` — :green:`Object` (optional).  Provider configurations
      for OAuth plugins.  Each key is a provider name matching the plugin
      filename (e.g., ``google`` for ``auth-plugins/google.js``), or a
      name of your choice when using the generic plugin with
      ``plugin: "generic"``.  Each entry may include ``icon`` (URL to a
      small image for the login button) and ``label`` (display name).
      See `OAuth Plugins`_ below.

    * ``displayCookie`` — :green:`Object` (optional).  When set, a
      second cookie is created on login containing selected session fields
      in URL-safe base64-encoded JSON.  Unlike the session cookie, this
      cookie is **not** HttpOnly, so client-side JavaScript can read it to
      display the logged-in user's name, picture, etc. on static pages
      that have no server-side processing.

      Properties:

      - ``cookieName`` — cookie name (default: ``"rp_user"``).
      - ``fields`` — :green:`Array` of :green:`Strings`, the session
        fields to include (default: ``["name", "picture"]``).

      The cookie is set alongside the session cookie on login (including
      OAuth logins) and cleared on logout.  The value is URL-safe base64
      (using ``-`` and ``_`` instead of ``+`` and ``/``), so client-side
      code must convert before calling ``atob()``:

      .. code-block:: javascript

          var raw = cookieValue.replace(/-/g, "+").replace(/_/g, "/");
          var info = JSON.parse(atob(raw));
          // info.name, info.picture, etc.

    * ``protectedPaths`` — :green:`Object`.  Maps URL path prefixes to
      access rules.  See `Protected Paths`_ below.

Protected Paths
~~~~~~~~~~~~~~~

Paths not listed in ``protectedPaths`` are public — no authentication is
required.  For public paths, ``req.userAuth`` is still populated when a
valid session exists, enabling personalization on public pages.

Each entry in ``protectedPaths`` specifies:

    * ``level`` — :green:`Number`.  The required privilege level.  The
      user's ``authLevel`` must be less than or equal to this value.
      Lower numbers represent higher privilege (e.g., ``0`` = admin).

    * ``redirect`` — :green:`String` (optional).  URL template for 302
      redirect on denied page requests.  ``$origin`` is replaced with
      the URL-encoded originally requested path.  If omitted, denied
      requests receive a plain 403.

Path matching uses prefix comparison with full inheritance: a rule for
``"/admin/"`` protects ``"/admin/reports/quarterly/file.html"``.  When
multiple prefixes match, the most specific (longest) match wins.

.. code-block:: javascript

    protectedPaths: {
        "/admin/": {
            level: 0,                                  // admin only
            redirect: "/apps/auth/login?returnTo=$origin"   // redirect pages
        },
        "/admin/public/": {
            level: 50                                  // override: any user
        },
        "/private/": {
            level: 1                                   // editors and above
        }
    }

The Auth Function Contract
--------------------------

The ``rampart-auth.so`` module (or any custom auth module) exports a
single function.  The server calls this function on every request that
passes through an app module, and on file requests to protected paths.

For file requests
~~~~~~~~~~~~~~~~~

The function receives a minimal request object:

.. code-block:: javascript

    {
        ip:      "192.168.1.1",
        path:    { path: "/private/page.html" },
        query:   { v: "123" },
        headers: { "Host": "example.com", "Cookie": "..." },
        cookies: { rp_session: "token_value" }
    }

Return values:

    * ``true`` — serve the file.
    * Anything else — the server sends 403 (or 302 redirect based on
      ``redirectExtensions`` and the path's ``redirect`` configuration).

For app module requests
~~~~~~~~~~~~~~~~~~~~~~~

The function receives the full ``req`` object (with ``method``, ``body``,
``postData``, etc.).  Since ``req`` is passed by reference, any
modifications (such as setting ``req.userAuth``) are made in-place and
are visible to the endpoint handler regardless of what the function
returns.

Return values:

    * Any truthy value (``true``, a number, the ``req`` object, etc.)
      — pass through to the endpoint handler.  The ``req`` object
      already has any modifications the function made.
    * ``false`` — the server sends 403.
    * ``{redirect: "/path"}`` — the server sends a 302 redirect.

The req.userAuth Object
~~~~~~~~~~~~~~~~~~~~~~~

When a valid session is found, the auth function attaches the session
data to ``req.userAuth``.  This object contains all fields stored in
the LMDB session record:

.. code-block:: javascript

    req.userAuth = {
        username:     "alice",
        name:         "Alice Smith",
        email:        "alice@example.com",
        authLevel:    0,
        authMethod:   "password",
        csrfToken:    "random_token_string",
        expires:      1744300800,
        lastRefresh:  1744214400,
        created:      1744214400
    }

App modules use this to make authorization decisions:

.. code-block:: javascript

    module.exports = function(req) {
        if (!req.userAuth) {
            return {status: 302, headers: {"location": "/apps/auth/login"}};
        }
        if (req.userAuth.authLevel > 0) {
            return {html: "<h1>Admin access required</h1>", status: 403};
        }
        return {html: "<h1>Welcome " + req.userAuth.name + "</h1>"};
    };

CSRF Protection
~~~~~~~~~~~~~~~

When ``csrf`` is enabled (the default), POST, PUT, DELETE, and PATCH
requests from authenticated sessions must include the CSRF token.  The
token is available to templates and JavaScript via
``req.userAuth.csrfToken``.

In HTML forms:

.. code-block:: html

    <form method="POST" action="/apps/myapp/update">
        <input type="hidden" name="_csrf" value="<%= req.userAuth.csrfToken %>">
        <!-- other fields -->
        <button type="submit">Save</button>
    </form>

In AJAX requests:

.. code-block:: javascript

    fetch("/apps/myapp/update", {
        method: "POST",
        headers: {
            "Content-Type": "application/json",
            "X-CSRF-Token": userAuth.csrfToken
        },
        body: JSON.stringify(data)
    });

Requests without a valid CSRF token receive a 403 response.
GET and HEAD requests are always exempt.
Paths listed in ``csrfExemptPaths`` are also exempt.

Custom Auth Modules
-------------------

The ``authMod`` property accepts a :green:`Function` or
:green:`String` for custom authentication logic.  A custom module
must export a single function with the same contract as described in
`The Auth Function Contract`_ above.

Inline function example:

.. code-block:: javascript

    server.start({
        authMod: function(req) {
            // custom auth using a header instead of cookies
            var token = req.headers ? req.headers['X-API-Key'] : null;
            if (req.method) {
                // app module request
                if (token === "my-secret-key")
                    req.userAuth = {username: "api-user", authLevel: 0};
                return req;
            } else {
                // file request
                return (token === "my-secret-key");
            }
        },
        authModConf: working_directory + '/auth-conf.js',
        // ...
    });

Module example (``my-auth.js``):

.. code-block:: javascript

    // my-auth.js
    var Lmdb = require("rampart-lmdb");
    var db = new Lmdb.init("data/my-auth-db", true, {conversion: "json"});

    module.exports = function(req) {
        var token = req.cookies ? req.cookies.my_session : null;
        if (!token) return req.method ? req : false;

        var session = db.get(null, token);
        if (!session) return req.method ? req : false;

        if (req.method) {
            req.userAuth = session;
            return req;
        }
        return true;
    };

Then in the server configuration:

.. code-block:: javascript

    authMod: "my-auth",
    authModConf: working_directory + '/auth-conf.js',


The auth.js Administration Module
---------------------------------

The ``auth.js`` file in ``web_server/apps/`` serves two purposes:

1. **Server module** — when loaded by the web server, it provides HTTP
   endpoints for login, logout, password management, and an
   administrative interface.

2. **CLI tool** — when run directly with ``rampart apps/auth.js``, it
   provides a command-line administration tool for managing users and
   sessions.

This module handles all user management operations in JavaScript.
These operations (login, user creation, password hashing) are not
performance-critical — they happen infrequently compared to the
per-request session validation performed by the C module.

CLI Administration
~~~~~~~~~~~~~~~~~~

Run ``rampart apps/auth.js`` with no arguments for interactive initial
setup, or with a command:

.. code-block:: none

    Usage: rampart apps/auth.js <command> [args]

    Commands:
      add    <username> <password> [level]   Create a new user (default level: 50)
      del    <username>                      Delete a user and their sessions
      list                                   List all users
      passwd <username> <password>            Change password
      reset  <username> <password>            Reset password (force change on login)
      level  <username> <level>               Set auth level
      sessions [username]                     List active sessions
      revoke <username>                       Revoke all sessions for user
      cleanup                                 Remove expired sessions and tokens
      setup                                   Interactive initial setup

When run with no users in the database, or with the ``setup`` command,
an interactive setup wizard prompts for an admin username and password.

HTTP Endpoints
~~~~~~~~~~~~~~

When the server is running with ``authMod`` enabled, the following
endpoints are available:

**Public (no authentication required):**

    * ``POST /apps/auth/login`` — Accepts ``username``, ``password``,
      and optional ``returnTo`` in the POST body.  On success, sets a
      session cookie and redirects.  On failure, redirects to
      ``/apps/auth/login?error=invalid``.  Error codes include ``invalid``
      (bad credentials), ``locked`` (account locked out), and
      ``unverified`` (email not verified).

    * ``GET /apps/auth/logout`` — Clears the session cookie and
      redirects to ``/``.

    * ``GET|POST /apps/auth/register`` — Self-registration form.
      Only available when ``allowRegistration`` is ``true``.  Creates
      the account and sends a verification email if
      ``requireEmailVerification`` is enabled.

    * ``GET /apps/auth/verify-email?token=...`` — Email verification
      link.  Validates the token and marks the account as verified.

    * ``GET|POST /apps/auth/resend-verification`` — Resend the
      verification email for a username.

    * ``GET|POST /apps/auth/request-reset`` — Request a password reset
      via email.  Sends a reset link to the user's email address.
      Uses a generic response to avoid revealing whether the user
      exists.

    * ``GET|POST /apps/auth/do-reset`` — Token-based password reset
      (for email reset links).

**Requires authentication:**

    * ``GET|POST /apps/auth/force-reset`` — Forced password change page,
      shown when ``mustResetPassword`` is ``true`` in the session.

**Admin only (protected at level 0):**

    * ``GET /apps/auth/admin/`` — User list with edit, reset, and
      delete actions.

    * ``GET|POST /apps/auth/admin/create-user`` — Create user form.

    * ``GET|POST /apps/auth/admin/edit-user`` — Edit user form.

    * ``GET|POST /apps/auth/admin/reset-pw`` — Admin password reset
      with option to force change on next login.

    * ``GET|POST /apps/auth/admin/delete-user`` — Delete user
      confirmation.

    * ``GET|POST /apps/auth/admin/sessions`` — Session list with
      revoke buttons.

Programmatic API
~~~~~~~~~~~~~~~~

When loaded via ``require()``, the module exports both the HTTP
endpoints (for server mapping) and a programmatic API:

.. code-block:: javascript

    var auth = require("apps/auth");
    auth.init();

    // User management
    auth.createUser({username: "alice", password: "secret", authLevel: 10});
    auth.getUser("alice");
    auth.listUsers();
    auth.updateUser("alice", {name: "Alice Smith", email: "alice@example.com"});
    auth.deleteUser("alice");

    // Custom user properties
    auth.updateUser("alice", {editor: true, department: "news"});

    // Password management
    auth.changePassword("alice", "old_password", "new_password");
    auth.adminResetPassword("alice", "temp_password", true); // force change

    // Password reset flow (for email integration)
    var reset = auth.requestPasswordReset("alice");
    // reset.resetToken and reset.resetUrl available for email
    auth.completePasswordReset(reset.resetToken, "new_password");

    // Session management
    var result = auth.login("alice", "secret");
    // result.token, result.cookie, result.session
    auth.logout(result.token);
    auth.listSessions("alice");
    auth.deleteSession(token);
    auth.deleteAllSessions("alice");
    auth.refreshSessions("alice");

    // Self-registration and email verification
    auth.register({username: "bob", password: "secret7", email: "bob@example.com"});
    auth.sendVerificationEmail("bob");
    auth.verifyEmail(verificationToken);

    // Password reset via email
    auth.sendPasswordResetEmail("alice");

    // Expired record cleanup
    auth.cleanupExpired();

    // Low-level email sending (for custom flows)
    auth.sendAuthEmail("user@example.com", "Subject", "<p>HTML body</p>");

All password changes and resets automatically revoke existing sessions,
forcing the user to re-authenticate with the new password.

User Records vs Session Records
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The auth system stores data in two LMDB databases:

    * **User records** — stored in the ``"users"`` database, keyed by
      username.  These are the canonical source of user information:
      username, email, password hash, auth level, and any custom
      properties added via ``updateUser()``.

    * **Session records** — stored in the ``"default"`` database, keyed
      by session token.  These are **denormalized copies** of user data,
      created at login time.  The C auth module reads session records on
      every request (a single LMDB lookup) and populates ``req.userAuth``
      with their contents.

When a user logs in, ``login()`` copies all properties from the user
record into the new session record (excluding ``passwordHash``).  This
means the C module needs only one fast LMDB read per request — it never
touches the user database.

The tradeoff: if you update a user's properties (e.g., add
``editor: true`` or change ``authLevel``), existing sessions still have
the old values.  The user would see the changes only after logging in
again.  To push changes to active sessions immediately, use
``refreshSessions()``:

.. code-block:: javascript

    // add a custom property to the user record
    auth.updateUser("alice", {editor: true, canPublish: false});

    // update all of alice's active sessions with the new properties
    auth.refreshSessions("alice");
    // returns {ok: true, updated: 2}

After ``refreshSessions()``, the next request from any of alice's
sessions will see ``req.userAuth.editor === true`` — no re-login
required.

Custom Properties in Endpoint Callbacks
""""""""""""""""""""""""""""""""""""""""

Any property stored on a user record appears in ``req.userAuth`` after
login (or after ``refreshSessions()``).  This enables role-based and
attribute-based access control in endpoint callbacks:

.. code-block:: javascript

    // set up roles via custom properties
    auth.updateUser("alice", {editor: true, department: "news"});
    auth.updateUser("bob",   {editor: false, department: "sports"});

    // in an endpoint callback
    function editArticle(req) {
        if (!req.userAuth || !req.userAuth.editor)
            return {status: 403, html: "Editors only"};

        if (req.userAuth.department !== article.department)
            return {status: 403, html: "Wrong department"};

        // proceed with edit...
    }

Session Data Model
~~~~~~~~~~~~~~~~~~

Session records are JSON objects containing all user properties
(except ``passwordHash``) plus session-specific fields:

.. code-block:: javascript

    {
        "username":     "alice",
        "name":         "Alice Smith",
        "email":        "alice@example.com",
        "authLevel":    0,
        "authMethod":   "password",
        "editor":       true,
        "department":   "news",
        "csrfToken":    "url_safe_base64_token",
        "expires":      1744300800,
        "lastRefresh":  1744214400,
        "created":      1744214400,
        "mustResetPassword": false
    }

The ``authMethod`` field indicates how the user authenticated:
``"password"`` for email/password login, ``"google"`` for Google OAuth,
``"facebook"`` for Facebook OAuth, or any custom value from a plugin.
Custom properties (like ``editor`` and ``department`` above) are
application-defined and can be any JSON-serializable value.

Theme Support
~~~~~~~~~~~~~

The admin interface supports theming via an optional module at
``data/auth/admin-theme.js``:

.. code-block:: javascript

    module.exports = {
        beginHtml: '<!DOCTYPE html><html><head>...<link rel="stylesheet"'
                 + ' href="/css/site.css"></head><body><nav>...</nav>'
                 + '<div class="content">',
        endHtml:   '</div><footer>...</footer></body></html>'
    };

If the theme module is not found, the admin pages render with a minimal
built-in stylesheet.

OAuth Plugins
~~~~~~~~~~~~~

The auth system supports social login via a plugin architecture.
Plugins are JavaScript files placed in the ``apps/auth-plugins/``
directory.  Three plugins are included: Google, Facebook, and a generic
OAuth 2.0 plugin that can be configured for any standard provider.

When the server starts, ``auth.js`` scans the plugin directory and
calls each plugin's ``init()`` function with the full auth configuration
and an API object.  If the plugin's required configuration is present
(e.g., ``oauth.google`` in ``auth-conf.js``), it activates and
registers its endpoints.

**How it works:**

1. The plugin registers a *start* endpoint and a *callback* endpoint.
2. The start endpoint generates a random state token (stored in LMDB
   with a 10-minute expiry), then redirects the browser to the
   provider's authorization page.
3. The provider authenticates the user and redirects back to the
   callback URL with an authorization code and state token.
4. The callback endpoint validates the state token, exchanges the code
   for an access token, fetches the user's profile, and returns a
   ``userData`` object to ``auth.js``.
5. ``auth.js`` creates or updates the user record and creates a session.
   All cookie handling (session cookie and display cookie) is managed
   by ``auth.js``, not the plugin.

Included Plugins
""""""""""""""""

**Google** (``auth-plugins/google.js``)

Uses OpenID Connect.  The user's Google ``sub`` (subject identifier)
becomes the username, prefixed with ``google_``.

Configuration:

.. code-block:: javascript

    oauth: {
        google: {
            clientId:     "your-client-id.apps.googleusercontent.com",
            clientSecret: "your-client-secret",
            callbackUrl:  "https://example.com/apps/auth/oauth/google/callback"
        }
    }

Endpoints:

  * ``GET /apps/auth/oauth/google`` — starts the OAuth flow.  Accepts
    an optional ``returnTo`` query parameter.
  * ``GET /apps/auth/oauth/google/callback`` — handles the callback
    from Google.

To set up a Google OAuth client:

1. Go to the `Google Cloud Console <https://console.cloud.google.com/>`_.
2. Create or select a project.
3. Navigate to **APIs & Services > Credentials**.
4. Create an **OAuth 2.0 Client ID** (Web application type).
5. Add your callback URL to **Authorized redirect URIs**.
6. Copy the Client ID and Client Secret into ``auth-conf.js``.

**Facebook** (``auth-plugins/facebook.js``)

Uses the Facebook Graph API.  The user's Facebook ID becomes the
username, prefixed with ``fb_``.

Configuration:

.. code-block:: javascript

    oauth: {
        facebook: {
            clientId:     "your-app-id",
            clientSecret: "your-app-secret",
            callbackUrl:  "https://example.com/apps/auth/oauth/facebook/callback",
            apiVersion:   "v21.0"    // optional, defaults to "v21.0"
        }
    }

Endpoints:

  * ``GET /apps/auth/oauth/facebook`` — starts the OAuth flow.
    Accepts an optional ``returnTo`` query parameter.
  * ``GET /apps/auth/oauth/facebook/callback`` — handles the callback
    from Facebook.

To set up a Facebook app:

1. Go to `developers.facebook.com <https://developers.facebook.com/>`_.
2. Create a new app (Consumer type).
3. Add **Facebook Login** to the app.
4. Under **Facebook Login > Settings**, add your callback URL to
   **Valid OAuth Redirect URIs**.
5. Copy the App ID and App Secret into ``auth-conf.js``.
6. The app must be in **Live** mode for non-developer users to log in.

**Generic** (``auth-plugins/generic.js``)

Handles any standard OAuth 2.0 or OpenID Connect provider through
configuration.  Supports PKCE (Proof Key for Code Exchange) for
providers that require it (e.g., Twitter/X).  A single plugin file
can handle multiple providers — each entry in ``oauth`` with
``plugin: "generic"`` gets its own pair of endpoints.

Configuration (GitHub example):

.. code-block:: javascript

    oauth: {
        github: {
            plugin:       "generic",
            authorizeUrl: "https://github.com/login/oauth/authorize",
            tokenUrl:     "https://github.com/login/oauth/access_token",
            userInfoUrl:  "https://api.github.com/user",
            emailUrl:     "https://api.github.com/user/emails",
            clientId:     "your-client-id",
            clientSecret: "your-client-secret",
            callbackUrl:  "https://example.com/apps/auth/oauth/github/callback",
            scope:        "user:email",
            fieldMap: {
                id:       "id",
                name:     "name",
                email:    "email",
                picture:  "avatar_url"
            }
        }
    }

Configuration (Twitter/X example with PKCE):

.. code-block:: javascript

    oauth: {
        twitter: {
            plugin:       "generic",
            authorizeUrl: "https://twitter.com/i/oauth2/authorize",
            tokenUrl:     "https://api.twitter.com/2/oauth2/token",
            userInfoUrl:  "https://api.twitter.com/2/users/me?user.fields=profile_image_url",
            clientId:     "your-client-id",
            clientSecret: "your-client-secret",
            callbackUrl:  "https://example.com/apps/auth/oauth/twitter/callback",
            scope:        "users.read tweet.read",
            pkce:         true,
            fieldMap: {
                id:       "id",
                name:     "name",
                email:    "email",
                picture:  "profile_image_url"
            }
        }
    }

Generic plugin configuration properties:

  * ``plugin`` — must be ``"generic"`` to select this plugin.
  * ``authorizeUrl`` — the provider's authorization endpoint.
  * ``tokenUrl`` — the provider's token exchange endpoint.
  * ``userInfoUrl`` — the provider's user info endpoint (optional
    if the provider returns an ``id_token`` JWT in the token response).
  * ``clientId``, ``clientSecret`` — OAuth credentials from the provider.
  * ``callbackUrl`` — your callback URL, registered with the provider.
  * ``scope`` — space-separated scopes (default: ``"openid profile email"``).
  * ``pkce`` — :green:`Boolean`, enable PKCE with S256 challenge method
    (default: ``false``).
  * ``emailUrl`` — :green:`String` (optional).  A separate endpoint to
    fetch the user's email when the main profile does not include one.
    Some providers (e.g., GitHub) require a separate API call to retrieve
    email addresses.  The response may be an array of objects with
    ``email``, ``primary``, and ``verified`` fields (GitHub format), an
    array of strings, or an object with an ``email`` field.  The plugin
    selects the primary verified email when available.
  * ``fieldMap`` — :green:`Object`, maps standard field names to the
    provider's field names.  Properties: ``id``, ``name``, ``email``,
    ``picture``.  Defaults to OIDC standard names (``sub``, ``name``,
    ``email``, ``picture``).

The plugin handles two response formats automatically:

  * **OIDC providers** — the ``id_token`` JWT in the token response
    is decoded directly; no ``userInfoUrl`` fetch is needed.
  * **Plain OAuth 2.0 providers** — the ``access_token`` is used to
    fetch the user profile from ``userInfoUrl`` via
    ``Authorization: Bearer`` header.

If the user info response wraps the profile in a ``data`` object
(as Twitter/X does), it is automatically unwrapped.

Using OAuth on Login Pages
""""""""""""""""""""""""""

When plugins are loaded, the built-in login page at
``/apps/auth/login`` automatically shows a button for each active
provider with a small icon and label.  Icons are resolved in this
order:

1. The ``icon`` property in the provider's config (a URL to any image).
2. The ``favicon.ico`` from the provider's website.

The button label defaults to the capitalized provider name (e.g.,
``github`` becomes ``Github``), with correct casing for well-known
names (``GitHub``, ``LinkedIn``, etc.).  Override with the ``label``
property:

.. code-block:: javascript

    oauth: {
        github: {
            plugin: "generic",
            label:  "GitHub",           // custom display name
            icon:   "/images/gh.png",   // custom icon (optional)
            // ... other config ...
        }
    }

For custom login pages, link to the plugin's start endpoint
with an optional ``returnTo`` parameter:

.. code-block:: html

    <a href="/apps/auth/oauth/google?returnTo=/dashboard">
        Sign in with Google
    </a>
    <a href="/apps/auth/oauth/facebook?returnTo=/dashboard">
        Sign in with Facebook
    </a>

OAuth User Records
""""""""""""""""""

Users created via OAuth have the following properties:

.. code-block:: javascript

    {
        username:      "google_123456789",  // or "fb_123456789"
        name:          "Alice Smith",
        email:         "alice@example.com",
        picture:       "https://...",       // profile picture URL
        authMethod:    "google",            // or "facebook"
        oauthProvider: "google",            // or "facebook"
        oauthId:       "google:123456789",  // or "facebook:123456789"
        emailVerified: true,
        authLevel:     50                   // default level for new users
    }

On subsequent logins, the user's name, email, picture, and other
provider data are updated from the provider's response, keeping the
local record in sync.

Writing a Custom Plugin
"""""""""""""""""""""""

A plugin is a JavaScript file in ``apps/auth-plugins/`` that exports
three properties:

.. code-block:: javascript

    // apps/auth-plugins/my-provider.js

    exports.name = "my-provider";

    exports.init = function(conf, api) {
        // conf is the full auth-conf.js object.
        // api provides: init(), createUser(), getUser(), updateUser(),
        //   login(), logout(), generateToken(), getDbPath(),
        //   getCookieName(), refreshSessions(), createOAuthSession().
        //
        // Return true if the plugin should activate, false to skip.

        if (!conf.oauth || !conf.oauth.myProvider) return false;

        // store config, api references for later use
        return true;
    };

    exports.endpoints = {
        // Start endpoint — redirect to provider
        "/oauth/my-provider": function(req) {
            var returnTo = (req.query && req.query.returnTo)
                         ? req.query.returnTo : "/";

            // generate state token, store in LMDB, redirect to provider
            // ...

            return {
                status: 302,
                headers: {"location": providerAuthUrl}
            };
        },

        // Callback endpoint — handle provider response
        "/oauth/my-provider/callback": function(req) {
            // validate state, exchange code for token, fetch profile
            // ...

            // return userData — auth.js handles session and cookies
            return {
                ok: true,
                returnTo: savedReturnTo,
                userData: {
                    username:      "myprov_" + uniqueId,
                    name:          "User Name",
                    email:         "user@example.com",
                    picture:       "https://...",
                    authMethod:    "my-provider",
                    oauthProvider: "my-provider",
                    oauthId:       "my-provider:" + uniqueId,
                    emailVerified: true
                }
            };
        }
    };

The callback endpoint's return value is handled by ``auth.js``:

  * If it has a ``status`` property (e.g., a redirect on error), it is
    passed through as-is.
  * If it has ``ok: true`` and ``userData``, ``auth.js`` calls
    ``createOAuthSession(userData)`` to create or update the user,
    create the session, and build all cookies (session cookie and
    display cookie).
  * If ``ok`` is falsy, the user is redirected to the login page with
    an error.

The ``userData`` object can contain any properties.  Standard fields
(``username``, ``name``, ``email``, ``picture``, ``authMethod``,
``oauthProvider``, ``oauthId``, ``emailVerified``) are recognized by
``auth.js``; additional fields are stored on the user record and
appear in ``req.userAuth`` after login.

Hooks for Custom Authentication Flows
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The configuration file supports optional callback hooks for extending
the authentication flow:

.. code-block:: javascript

    module.exports = {
        // ... standard config ...

        // Called after password verification, before session creation.
        // Return true to proceed, {redirect: "/2fa"} for 2FA, false to deny.
        onLogin: function(user, req) {
            if (user.twoFactorEnabled)
                return {redirect: "/apps/auth/2fa?user=" + user.username};
            return true;
        },

        // Called after a session is created. Use for audit logging.
        onSessionCreated: function(user, session, req) { },

        // Called on logout. Use for audit logging.
        onLogout: function(user, req) { }
    };

Security Considerations
-----------------------

    * **Session tokens** are 32 bytes of cryptographically random data,
      URL-safe base64 encoded.

    * **Passwords** are hashed using ``crypto.passwd()`` (SHA-512 crypt
      with random salt).

    * **Session cookies** are set with ``HttpOnly``, ``SameSite=Lax``,
      and ``Secure`` flags by default.  The ``Secure`` flag means
      cookies are only sent over HTTPS.  Set
      ``cookieFlags: {secure: false}`` explicitly if running without
      HTTPS.  The server cannot auto-detect HTTPS because it may be
      behind a reverse proxy.

    * **CSRF tokens** are checked on all state-changing requests from
      authenticated sessions.

    * **Account lockout** prevents brute-force password attacks (5
      attempts in 5 minutes, 15-minute lockout, all configurable).

    * **Password policy** enforces a minimum length (default 7
      characters, configurable via ``minPasswordLength``).

    * **Timing attack protection** — login and registration use a
      constant-time pattern.  When a username does not exist, a dummy
      password hash is computed to match the timing of a real password
      check, preventing user enumeration via response time measurement.

    * **Session revocation** occurs automatically on password changes
      and password resets.

    * **Email cooldown** — verification and password reset emails are
      limited to one per username per 60 seconds, preventing email
      bombing regardless of the number of source IPs.

    * **Expired record cleanup** — expired sessions, reset tokens,
      verification tokens, and lockout records are periodically cleaned
      from LMDB (approximately once per 20 logins).  Manual cleanup
      is also available via ``rampart apps/auth.js cleanup``.

    * **LMDB file permissions** are set to ``0600`` when the server
      starts as root and switches to an unprivileged user.

    * **Open redirect protection** — the ``returnTo`` parameter in
      login redirects is validated to be a local path.

    * **OAuth state tokens** — OAuth flows use a random state token
      stored in LMDB with a 10-minute expiry to prevent CSRF attacks
      during the authorization redirect.  The token is consumed on
      first use.

    * **Display cookie** — the optional display cookie is **not**
      HttpOnly and is readable by client-side JavaScript.  It should
      contain only non-sensitive display data (name, picture URL).
      It does not grant authentication — the HttpOnly session cookie
      is the sole source of auth.

    * **Rate limiting** — the server's ``rateLimit`` configuration
      (see :doc:`rampart-server`) can be used to limit requests to
      auth endpoints at the C level, before any JavaScript runs.

Quick Start
-----------

1. Enable authentication in ``web_server_conf.js``:

   .. code-block:: javascript

       authMod:     true,
       authModConf: working_directory + '/auth-conf.js',

2. Run initial setup:

   .. code-block:: none

       cd web_server
       rampart apps/auth.js

   Follow the prompts to create an admin user.

3. Start (or restart) the server:

   .. code-block:: none

       rampart web_server_conf.js start

4. Visit ``http://yourserver/apps/auth/login`` to log in.

5. Visit ``http://yourserver/apps/auth/admin/`` for the admin panel.

6. If not using HTTPS, set ``cookieFlags: {secure: false}`` in
   ``auth-conf.js``.  The ``Secure`` flag defaults to ``true``.

7. Edit ``auth-conf.js`` to configure protected paths, session
   expiry, CSRF, and lockout settings.

8. To enable OAuth login, add provider credentials to ``auth-conf.js``:

   .. code-block:: javascript

       oauth: {
           google: {
               clientId:     "...",
               clientSecret: "...",
               callbackUrl:  "https://yourserver/apps/auth/oauth/google/callback"
           }
       }

   Place the corresponding plugin file (e.g., ``google.js``) in
   ``apps/auth-plugins/``.  For other providers, use the generic
   plugin with ``plugin: "generic"`` and configure the URLs and
   field mapping for that provider.

9. To show user info on static pages, add a display cookie:

   .. code-block:: javascript

       displayCookie: {
           cookieName: "rp_user",
           fields: ["name", "picture"]
       }
