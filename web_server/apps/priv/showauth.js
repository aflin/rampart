/*
    Sample protected module — requires authentication.

    Demonstrates how to use req.userAuth in an app module.
    Displays the authenticated user's information with syntax
    highlighting, similar to test_modules/hlshowreq.js.

    This path (/apps/priv/) should be listed in auth-conf.js
    protectedPaths to require login.  If not protected, the page
    still works but shows that no user is authenticated.
*/
rampart.globalize(rampart.utils);

var Sql = require("rampart-sql");
rampart.globalize(Sql, ["sandr"]);

function sandrHighlight(json) {
    var search_and_replace = [
        ['>>"=\\space*:=\\space*"=[^"]+',                          '": "<span class="string">\\6</span>'],
        ['>>"=\\space*:=\\space*true=',                            '": <span class="boolean">\\5</span>'],
        ['>>"=\\space*:=\\space*false=',                           '": <span class="boolean">\\5</span>'],
        ['>>"=\\space*:=\\space*null=',                            '": <span class="null">\\5</span>'],
        ['>>"=\\space*:=\\space*[+\\-\\.\\digit]+',                '": <span class="number">\\5</span>'],
        ['>>"=[^"]+"=\\space*:',                                   '"<span class="key">\\2</span>":'],
        ['>>[\\[,]=\\space*"\\P=[^"]+\\F"=\\space*[\\],]=',       '<span class="string">\\4</span>'],
        ['>>[\\[,]=\\space\\P*true=\\F\\space*[\\],]=',            '<span class="boolean">\\3</span>'],
        ['>>[\\[,]=\\space\\P*false=\\F\\space*[\\],]=',           '<span class="boolean">\\3</span>'],
        ['>>[\\[,]=\\space\\P*null=\\F\\space*[\\],]=',            '<span class="null">\\3</span>'],
        ['>>[\\[,]=\\space\\P*[+\\-\\.\\digit]+\\F\\space*[\\],]=','<span class="number">\\3</span>']
    ];
    return sandr(search_and_replace, json);
}

var css =
    'pre { padding: 5px; margin: 5px; }\n' +
    'body { background-color: #272822; color: #f8f8f2; font-family: sans-serif; }\n' +
    '.number { color: #ae81ff; }\n' +
    '.string { color: #e6db74; }\n' +
    '.boolean { color: #ae81ff; }\n' +
    '.null { color: #ae81ff; }\n' +
    '.key { color: #a6e22e; }\n' +
    '.info { margin: 10px 5px; }\n' +
    '.info a { color: #66d9ef; }\n' +
    '.warn { color: #f92672; font-weight: bold; }\n';

function showauth(req) {
    var authInfo = {};

    if (req.userAuth) {
        authInfo.authenticated = true;
        authInfo.userAuth = req.userAuth;
        authInfo.note = "This information is set by the auth module on every request.";
    } else {
        authInfo.authenticated = false;
        authInfo.note = "No user is authenticated. Either this path is not protected, " +
                        "or no valid session cookie was sent.";
    }

    authInfo.requestInfo = {
        method: req.method,
        path: req.path,
        ip: req.ip,
        cookies: req.cookies,
        headers: req.headers
    };

    var str = sprintf('%4J', authInfo);

    return {
        html: '<html>\n' +
            '<head>\n' +
            '  <title>Auth Info</title>\n' +
            '  <style>' + css + '</style>\n' +
            '</head>\n' +
            '<body>\n' +
            '  <div class="info">\n' +
            '    <h3>Authentication Status</h3>\n' +
            (req.userAuth
                ? '    <p>Logged in as: <span class="string">' + sprintf("%H", req.userAuth.username) + '</span>' +
                  ' (level ' + req.userAuth.authLevel + ')</p>\n' +
                  '    <p><a href="/apps/auth/logout">Log out</a>' +
                  ' | <a href="/apps/auth/admin/">Admin Panel</a></p>\n'
                : '    <p class="warn">Not authenticated.</p>\n' +
                  '    <p><a href="/apps/auth/login?returnTo=' + sprintf("%U", req.path.path) + '">Log in</a></p>\n'
            ) +
            '  </div>\n' +
            '  <pre>' + sandrHighlight(str) + '</pre>\n' +
            '</body>\n' +
            '</html>'
    };
}

module.exports = showauth;
