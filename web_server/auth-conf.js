/*
    Authentication configuration for rampart-auth.

    This file is loaded as a JS module when authMod is enabled
    in web_server_conf.js:

        authMod:     true,
        authModConf: working_directory + '/auth-conf.js',

    All values shown below are defaults.

    !! WARNING !!
    The "secure" cookie flag below is set to false for initial
    testing over HTTP.  In production, you MUST either:
      1. Enable HTTPS and set secure to true (or remove cookieFlags
         entirely — it defaults to true).
      2. Run behind a reverse proxy that handles HTTPS.
    Leaving secure:false in production exposes session cookies
    to interception on the network.
*/

module.exports = {

    /* cookieName        String. Name of the session cookie.                    */
    cookieName: "rp_session",

    /* dbPath            String. Path to LMDB database directory.
                         Relative paths are resolved from the server root.      */
    dbPath: "data/auth",

    /* beginHtml / endHtml   Strings. HTML wrapper for all auth pages (login,
                         register, admin, password reset, etc.).  The auth module
                         generates page content inside a <div> — these strings
                         wrap around it.  Use $title for the page title.

                         If omitted, the defaults below are used, which link to
                         /css/auth.css for styling.  To customize the look, either
                         edit /css/auth.css or provide your own beginHtml/endHtml
                         that links to your site's stylesheet.

                         Defaults:                                              */
    //beginHtml: `<!DOCTYPE html>
    //<html><head><meta charset="utf-8">
    //<meta name="viewport" content="width=device-width, initial-scale=1">
    //<title>$title</title>
    //<link rel="stylesheet" href="/css/auth.css">
    //</head><body>`,

    //endHtml: `</body></html>`,

    /* Example with site branding:

    beginHtml: `<!DOCTYPE html>
    <html><head><meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>$title — My Site</title>
    <link rel="stylesheet" href="/css/site.css">
    </head><body>
    <nav class="site-nav"><a href="/">Home</a></nav>
    <main>`,

    endHtml: `</main>
    <footer><p>&copy; My Site</p></footer>
    </body></html>`,
    */

    /* csrf              Boolean. Enable CSRF protection on POST/PUT/DELETE/PATCH
                         requests from authenticated sessions. (default: true)  */
    csrf: true,

    /* csrfExemptPaths   Array of Strings. Path prefixes exempt from CSRF
                         checking. Useful for webhooks or external API endpoints
                         that receive POSTs from third parties.                 */
    //csrfExemptPaths: ["/apps/api/webhook/"],

    /* redirectExtensions  Array of Strings. File extensions that trigger a 302
                         redirect (to the path's redirect URL) instead of a 403
                         when access is denied. Requests for other extensions
                         (images, CSS, JS, etc.) get a plain 403.              */
    redirectExtensions: ["", ".html", ".htm", ".txt"],

    /* allowRegistration  Boolean. Enable the self-registration form at
                         /apps/auth/register. (default: false)                  */
    //allowRegistration: false,

    /* requireEmailVerification  Boolean. When true, users created via
                         self-registration must verify their email before
                         they can log in.  Users created via the admin panel
                         or CLI are verified by default. (default: true)        */
    //requireEmailVerification: true,

    /* siteUrl           String. Base URL of the site, used to construct
                         links in verification and password reset emails.
                         No trailing slash. (e.g. "https://example.com")       */
    //siteUrl: "",

    /* email             Object. SMTP configuration for sending verification
                         and password reset emails.  If omitted, email-based
                         features are disabled.  Uses rampart-email.js.

                         from:    sender address (required)
                         method:  "direct", "relay", "smtp", or "gmailApp"
                                  (default: "direct")

                         For method "relay":
                           relay:     relay hostname (default: "localhost")
                           relayPort: relay port (default: 25)

                         For method "smtp":
                           smtpUrl:  SMTP URL (e.g. "smtps://smtp.example.com:465")
                           user:     SMTP username
                           pass:     SMTP password

                         For method "gmailApp":
                           user:  Gmail address
                           pass:  16-character app password

                         Optional for all methods:
                           insecure:       skip TLS cert verification
                           requireSsl:     do not fall back to plain SMTP
                           timeout:        max total time in seconds (default: 30)
                           connectTimeout: connection timeout in seconds (default: 10)
    */
    //email: {
    //    from: "noreply@example.com",
    //    method: "direct"
    //},

    /* sessionExpiry     Number. Session lifetime in seconds. When a session is
                         refreshed, its new expiry is set to now + this value.  */
    sessionExpiry: 86400,       // 24 hours

    /* sessionRefresh    Number. Minimum interval in seconds between session
                         expiry refreshes. The session is only written back to
                         LMDB if this many seconds have passed since the last
                         refresh. Set to 0 to disable sliding expiry.          */
    sessionRefresh: 300,        // 5 minutes

    /* sessionRefreshUrgent  Number. If fewer than this many seconds remain
                         before the session expires, refresh immediately
                         regardless of sessionRefresh interval.                */
    sessionRefreshUrgent: 3600, // 1 hour

    /* cookieFlags       Object. Flags for the session cookie.
                         httpOnly: true     - not accessible to JavaScript (default: true)
                         sameSite: "Lax"    - CSRF protection level (default: "Lax")
                         secure:   true     - only send over HTTPS (default: true)
                         path:     "/"      - cookie path (default: "/")
                         See WARNING at top of file.                            */
    cookieFlags: { secure: false },  /* CHANGE TO true (or remove) FOR PRODUCTION */

    /* minPasswordLength Number. Minimum password length for user creation
                         and password changes. (default: 7)                    */
    //minPasswordLength: 7,

    /* lockoutAttempts   Number. Max failed login attempts before the account
                         is temporarily locked. Set to 0 to disable. (default: 5) */
    //lockoutAttempts: 5,

    /* lockoutWindow    Number. Time window in seconds for counting failed
                         login attempts. (default: 300 = 5 minutes)            */
    //lockoutWindow: 300,

    /* lockoutDuration  Number. How long the account stays locked in seconds.
                         (default: 900 = 15 minutes)                           */
    //lockoutDuration: 900,

    /* displayCookie     Object. Optional. When set, a second cookie is created
                         on login containing selected session fields in URL-safe
                         base64-encoded JSON.  Unlike the session cookie, this
                         cookie is NOT HttpOnly, so client-side JavaScript can
                         read it to show the user's name, picture, etc. on
                         static pages without server-side processing.

                         cookieName: cookie name (default: "rp_user")
                         fields:     array of session fields to include
                                     (default: ["name", "picture"])              */
    //displayCookie: {
    //    cookieName: "rp_user",
    //    fields: ["name", "picture"]
    //},

    /* oauth             Object. OAuth provider configurations.  Each key is a
                         provider name.  Dedicated plugins (google.js, facebook.js)
                         check for their own key.  The generic plugin handles any
                         entry with plugin:"generic".

                         Plugins are loaded from apps/auth-plugins/.  The login
                         page automatically shows buttons with icons for each
                         active provider.

                         Optional per-provider properties:
                           icon:   URL to a small icon for the login button
                           label:  display name (default: capitalized key name)  */

    /* Google OAuth — requires apps/auth-plugins/google.js
       Set up at: https://console.cloud.google.com/ > APIs & Services > Credentials
       Create an OAuth 2.0 Client ID (Web application type).                    */
    //oauth: {
    //    google: {
    //        clientId:     "your-client-id.apps.googleusercontent.com",
    //        clientSecret: "your-client-secret",
    //        callbackUrl:  "https://example.com/apps/auth/oauth/google/callback"
    //    },

    /* Facebook OAuth — requires apps/auth-plugins/facebook.js
       Set up at: https://developers.facebook.com/ > Create App > Add Facebook Login.
       App must be in Live mode for non-developer users.                        */
    //    facebook: {
    //        clientId:     "your-app-id",
    //        clientSecret: "your-app-secret",
    //        callbackUrl:  "https://example.com/apps/auth/oauth/facebook/callback",
    //        apiVersion:   "v21.0"     // optional, defaults to "v21.0"
    //    },

    /* Generic OAuth 2.0 — requires apps/auth-plugins/generic.js
       Works with any standard OAuth 2.0 / OpenID Connect provider.
       Set plugin:"generic" and configure the provider's URLs and field mapping.
       Supports PKCE (pkce:true) for providers that require it.
       emailUrl is optional — fetches email from a separate endpoint if the
       main profile doesn't include one (e.g. GitHub).                          */
    //    github: {
    //        plugin:       "generic",
    //        authorizeUrl: "https://github.com/login/oauth/authorize",
    //        tokenUrl:     "https://github.com/login/oauth/access_token",
    //        userInfoUrl:  "https://api.github.com/user",
    //        emailUrl:     "https://api.github.com/user/emails",
    //        clientId:     "your-client-id",
    //        clientSecret: "your-client-secret",
    //        callbackUrl:  "https://example.com/apps/auth/oauth/github/callback",
    //        scope:        "user:email",
    //        fieldMap: {
    //            id:       "id",
    //            name:     "name",
    //            email:    "email",
    //            picture:  "avatar_url"
    //        }
    //    }
    //},

    /* protectedPaths    Object. Maps URL path prefixes to access rules.
                         Paths not listed here are public (no auth required).
                         Full inheritance: "/admin/" protects "/admin/reports/".
                         Most specific prefix wins.

                         level:    Required privilege level. The user's authLevel
                                   must be <= this value (lower = higher privilege).

                         redirect: URL template for 302 redirect on denied page
                                   requests. $origin is replaced with the URL-encoded
                                   originally requested path. If omitted, denied
                                   requests get a plain 403.                    */
    protectedPaths: {
        // Auth admin panel — admin only (level 0)
        "/apps/auth/admin": {
            level: 0,
            redirect: "/apps/auth/login?returnTo=$origin"
        },
        // Sample protected app — any logged-in user (level 50)
        "/apps/priv/": {
            level: 50,
            redirect: "/apps/auth/login?returnTo=$origin"
        }
        // Add more protected paths as needed:
        //"/private/": {
        //    level: 1,
        //    redirect: "/apps/auth/login?returnTo=$origin"
        //}
    }
};
