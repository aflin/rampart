rampart.globalize(rampart.utils);
// https://github.com/aflin/rampart/blob/main/unsupported_extras/forkpty-term/html/console.html
var sample_env = {
    "SHELL":     "/bin/bash",
    "COLORTERM": "truecolor",
    "LANG":      "en_US.UTF-8",
    //"LS_COLORS": "rs=0:di=01;34:ln=01;36:mh=00:pi=40;33:so=01;35:do=01;35:bd=40;33;01:cd=40;33;01:or=40;31;01:mi=00:su=37;41:sg=30;43:ca=30;41:tw=30;42:ow=34;42:st=37;44:ex=01;32:*.tar=01;31:*.tgz=01;31:*.arc=01;31:*.arj=01;31:*.taz=01;31:*.lha=01;31:*.lz4=01;31:*.lzh=01;31:*.lzma=01;31:*.tlz=01;31:*.txz=01;31:*.tzo=01;31:*.t7z=01;31:*.zip=01;31:*.z=01;31:*.dz=01;31:*.gz=01;31:*.lrz=01;31:*.lz=01;31:*.lzo=01;31:*.xz=01;31:*.zst=01;31:*.tzst=01;31:*.bz2=01;31:*.bz=01;31:*.tbz=01;31:*.tbz2=01;31:*.tz=01;31:*.deb=01;31:*.rpm=01;31:*.jar=01;31:*.war=01;31:*.ear=01;31:*.sar=01;31:*.rar=01;31:*.alz=01;31:*.ace=01;31:*.zoo=01;31:*.cpio=01;31:*.7z=01;31:*.rz=01;31:*.cab=01;31:*.wim=01;31:*.swm=01;31:*.dwm=01;31:*.esd=01;31:*.jpg=01;35:*.jpeg=01;35:*.mjpg=01;35:*.mjpeg=01;35:*.gif=01;35:*.bmp=01;35:*.pbm=01;35:*.pgm=01;35:*.ppm=01;35:*.tga=01;35:*.xbm=01;35:*.xpm=01;35:*.tif=01;35:*.tiff=01;35:*.png=01;35:*.svg=01;35:*.svgz=01;35:*.mng=01;35:*.pcx=01;35:*.mov=01;35:*.mpg=01;35:*.mpeg=01;35:*.m2v=01;35:*.mkv=01;35:*.webm=01;35:*.webp=01;35:*.ogm=01;35:*.mp4=01;35:*.m4v=01;35:*.mp4v=01;35:*.vob=01;35:*.qt=01;35:*.nuv=01;35:*.wmv=01;35:*.asf=01;35:*.rm=01;35:*.rmvb=01;35:*.flc=01;35:*.avi=01;35:*.fli=01;35:*.flv=01;35:*.gl=01;35:*.dl=01;35:*.xcf=01;35:*.xwd=01;35:*.yuv=01;35:*.cgm=01;35:*.emf=01;35:*.ogv=01;35:*.ogx=01;35:*.aac=00;36:*.au=00;36:*.flac=00;36:*.m4a=00;36:*.mid=00;36:*.midi=00;36:*.mka=00;36:*.mp3=00;36:*.mpc=00;36:*.ogg=00;36:*.ra=00;36:*.wav=00;36:*.oga=00;36:*.opus=00;36:*.spx=00;36:*.xspf=00;36:",
    "TERM":      "xterm-256color",
    "TERMCAP":   ""
}

var allowRoot=false;
module.exports = function (req)
{
    req.cols=80;
    req.rows=40;

    /* first run, req.body is empty */
    if (!req.count) {
        
        // fork a new login, place object in req where we can get at 
        // it in subsequent websocket messages from this client
        try {
            if(allowRoot)
                req.con = forkpty("/bin/bash", {env:sample_env, appendEnv:true}, '-c', 'sleep 0.2; while true; do echo -n "login: "; read u; su - $u && break || echo "Login failed, please try again.";sleep 2; done');
            else
                req.con = forkpty("/bin/bash", {env:sample_env, appendEnv:true}, '-c', 'sleep 0.2; while true; do echo -n "login: "; read u; if [ "$u" == "root" ]; then echo "root login not allowed"; else su - $u && break || echo "Login failed, please try again.";sleep 2; fi; done');
        } catch(e) {
            req.wsSend( "Something bad happened to forkpty, seek higher ground quickly." );
            req.wsEnd();
        } 
  
        req.wsOnDisconnect(function(){
            req.con.close();
        });
      
        // what to do when we have data waiting to go.
        req.con.on("data", function(){
            //read data and send it (req.con.read returns a buffer, so it is sent as binary).
            req.wsSend(req.con.read());
        });
  
        req.con.resize(req.cols,req.rows);
        req.wsSend(stringToBuffer("The login promp is insufficient to secure this app.  You should at minimum restrict access to this page and only run over https\r\n\r\n"));

        return;
    }

    function docmd(c){
        var jcmd;
        try {
            // convert to string and parse JSON
            jcmd = JSON.parse(sprintf("%s", c));
        } catch(e) { /* typeof jcmd == undefined */ }
        if(typeof jcmd == 'object') {
            // currently only recognizes one command (resize)
            if(typeof jcmd.resize == 'object' ) {
                var r=jcmd.resize
                if (typeof r.cols == 'number' ) req.cols = r.cols;
                if (typeof r.rows == 'number' ) req.rows = r.rows;
                req.con.resize(req.cols,req.rows);
            }
        } else {
            fprintf(stderr, "Warning: console.js: failed to process JSON command in '%s'\n", c);
        }
    }


    // second and subsequent run.

    if(req.body.length)
    {
        // if pty proc has exited, write and other pty methods will be undefined
        if(!req.con.write) {
            req.wsSend(stringToBuffer("The shell is not communicating.\r\n"));
            return;
        }
        // if plain text message, direct it to pty
        if(!req.wsIsBin)
            req.con.write(req.body);
        // if binary, treat it as a command
        else
            docmd(req.body);        
    }
  
    return;
}
