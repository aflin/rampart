rampart.globalize(rampart.utils);

var tmpdir = process.scriptPath + '/tmp-test';
if (!stat(tmpdir)) mkdir(tmpdir);

var rawvec = rampart.vector.raw;
/* vector-test allows a test to return `{result, text}` so a diagnostic
   string can be appended to the name.  We coerce that shape into a
   plain pass/fail before handing it to the harness. */
var _baseTestFeature = new (require('./test-feature.js'))({prefix: "vector"});
function testFeature(name, test) {
    if (typeof test === 'function') {
        try { test = test(); }
        catch (e) { _baseTestFeature(name, function(){ throw e; }); return; }
    }
    if (getType(test) === 'Object') {
        if (typeof test.text === 'string') name += ' ' + test.text;
        test = test.result;
    }
    _baseTestFeature(name, test);
}
testFeature.exit = function() { _baseTestFeature.exit(); };

function dotests(main) {
    var pref="";
    if(!main)
        pref = "thr: ";
    //make a random vector:
    var myvec=[];
    for (var i=0;i<64;i++)
        myvec.push(rampart.utils.rand(-5.0,5.0));

    var mybvec=[];
    for (var i=0;i<64;i++)
        mybvec.push(rampart.utils.rand(-5000.0,5000.0));

    testFeature(pref + "convert number array to f64 and back", function(){
        var v64 = new rampart.vector('f64',myvec);
        var nvec = v64.toNumbers();
        var ret=0.0;

        for (var i=0; i<myvec.length; i++)
        {
            //printf("vec: %.2f - %.2f = %.8f\n", myvec[i], nvec[i], myvec[i] - nvec[i]);
            ret += myvec[i] - nvec[i];
        }
        return ret == 0.0;
    });

    testFeature(pref + "convert number array to f32 and back", function(){
        var v32 = new rampart.vector('f32',myvec);
        var nvec = v32.toNumbers();
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);

        return err < 0.00001;
    });


    testFeature(pref + "convert number array to f16 and back", function(){
        var v16 = new rampart.vector('f16',myvec);
        var nvec = v16.toNumbers();
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);
        return err < 0.1;
    });

    testFeature(pref + "convert number array to bf16 and back", function(){
        var b16 = new rampart.vector('bf16',mybvec);
        var nvec = b16.toNumbers();
        var err=0.0;

        for (var i=0; i<mybvec.length; i++)
        {
            err += Math.abs(mybvec[i] - nvec[i]);
            //printf("%f vs %f\n", mybvec[i], nvec[i]);
        }
        //printf("err=%f\n", err);
        return err < 500.0;
    });

    var vecsz=1024;
    testFeature(pref + "convert nums to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var u8 = new rampart.vector('u8',tvec);
        var nvec = u8.toNumbers();
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f64 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var f64 = new rampart.vector('f64', tvec);
        var u8 = f64.toU8();
        var f64out = u8.toF64(1/255);
        var nvec = f64out.toNumbers();
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f32 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));

        var f32 = new rampart.vector('f32', tvec);
        var u8 = f32.toU8();
        var f32out = u8.toF32(1/255);
        var nvec = f32out.toNumbers();

        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f16 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));

        var f16 = new rampart.vector('f16', tvec);
        var u8 = f16.toU8();
        var f16out = u8.toF16(1/255);
        var nvec = f16out.toNumbers();

        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert nums to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));

        var i8 = new rampart.vector('i8',tvec);
        var nvec = i8.toNumbers();
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f64 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));

        var f64 = new rampart.vector('f64', tvec);
        var i8 = f64.toI8();
        var f64out = i8.toF64(1/127); // 1/127 is default
        var nvec = f64out.toNumbers();

        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f32 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var f32 = new rampart.vector('f32', tvec);
        var i8 = f32.toI8();
        var f32out = i8.toF32(1/127); // 1/127 is default
        var nvec = f32out.toNumbers();

        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f16 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var f16 = new rampart.vector('f16', tvec);
        var i8 = f16.toI8();
        var f16out = i8.toF16(1/127); // 1/127 is default
        var nvec = f16out.toNumbers();

        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "normalize F64", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in64 = new rampart.vector('f64', tvec);

        in64.l2Normalize();

        var outNum = in64.toNumbers();

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });


    testFeature(pref + "normalize F32", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in32 = new rampart.vector('f32', tvec);

        in32.l2Normalize();

        var outNum = in32.toNumbers();

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F16", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-500.0,500.0));

        var in16 = new rampart.vector('f16', tvec);

        in16.l2Normalize();

        var outNum = in16.toNumbers();

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.001 && sum > 0.999;
    });

    testFeature(pref + "distance dot f64", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec64 = new rampart.vector("f64", tvec);
        var antivec64 = new rampart.vector("f64",antivec);

        tvec64.l2Normalize();
        antivec64.l2Normalize();

        var score = tvec64.distance(tvec64);
        var score2 = tvec64.distance(antivec64);

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance dot f32", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec32 = new rampart.vector("f32", tvec);
        var antivec32 = new rampart.vector("f32",antivec);

        tvec32.l2Normalize();
        antivec32.l2Normalize();

        var score = tvec32.distance(tvec32);
        var score2 = tvec32.distance(antivec32);

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });


    testFeature(pref + "distance dot f16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = new rampart.vector("f16", tvec);
        var antivec16 = new rampart.vector("f16",antivec);

        tvec16.l2Normalize();
        antivec16.l2Normalize();

        var score = tvec16.distance(tvec16);
        var score2 = tvec16.distance(antivec16);

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine f64", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec64 = new rampart.vector('f64',tvec);
        var antivec64 = new rampart.vector('f64', antivec);

        var score = 1 - tvec64.distance(tvec64, 'cos');
        var score2 = 1 - tvec64.distance(antivec64, 'cosine');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine f32", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec32 = new rampart.vector('f32',tvec);
        var antivec32 = new rampart.vector('f32', antivec);

        var score = 1 - tvec32.distance(tvec32, 'cos');
        var score2 = 1 - tvec32.distance(antivec32, 'cosine');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });


    testFeature(pref + "distance cosine f16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = new rampart.vector('f16',tvec);
        var antivec16 = new rampart.vector('f16', antivec);

        var score = 1 - tvec16.distance(tvec16, 'cos');
        var score2 = 1 - tvec16.distance(antivec16, 'cosine');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine bf16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = new rampart.vector('bf16',tvec);
        var antivec16 = new rampart.vector('bf16', antivec);

        var score = 1 - tvec16.distance(tvec16, 'cos');
        var score2 = 1 - tvec16.distance(antivec16, 'cosine');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine u8", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<1024;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec8 = new rampart.vector('u8',tvec);
        var antivec8 = new rampart.vector('u8', antivec);

        var score = 1 - tvec8.distance(tvec8, 'cos');
        var score2 = 1 - tvec8.distance(antivec8, 'cosine');

        /* The distance kernel now rebases u8 by the symmetric zeroPoint
         * (128) before cosine, so mirror-opposite vectors score ~ -1.0,
         * exactly like i8 — u8 "just works" with no manual rebase. */
        return {
            result : (score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8),
            text : sprintf("(180deg err=%.3f)", score2+1.0)
        };
    });

    testFeature(pref + "distance cosine i8", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<1024;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec8 = new rampart.vector('i8',tvec);
        var antivec8 = new rampart.vector('i8', antivec);

        var score = 1 - tvec8.distance(tvec8, 'cos');
        var score2 = 1 - tvec8.distance(antivec8, 'cosine');

        return {
            result : (score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8),
            text : sprintf("(180deg err=%.3f)", score2+1.0)
        };
        return score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8;
    });
    // ------------------------------------------------------------
    // L2 (Euclidean) and L2-squared distance tests.
    //   'euclidean' / 'l2'      -> true L2 (sqrt of sum of squares)
    //   'l2sq' / 'sqeuclidean'  -> squared L2
    // Each test exercises BOTH metrics and verifies euclidean==l2 (alias)
    // and euclidean^2 == l2sq.
    // ------------------------------------------------------------

    testFeature(pref + "distance l2/l2sq f64", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-5000.0, 5000.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec64 = new rampart.vector('f64',tvec);
        var antivec64 = new rampart.vector('f64', antivec);

        var d_same = tvec64.distance(tvec64, 'l2');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = tvec64.distance(antivec64, 'euclidean');
        var d_l2   = tvec64.distance(antivec64, 'l2');
        var d_l2sq = tvec64.distance(antivec64, 'l2sq');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-9;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-6;

        return {
            result : (Math.abs(d_same) < 1e-9 && errL2 < 1e-6 && errL2sq < 1e-6 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq f32", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-5000.0, 5000.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec32 = new rampart.vector('f32',tvec);
        var antivec32 = new rampart.vector('f32', antivec);

        var d_same = tvec32.distance(tvec32, 'l2');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = tvec32.distance(antivec32, 'euclidean');
        var d_l2   = tvec32.distance(antivec32, 'l2');
        var d_l2sq = tvec32.distance(antivec32, 'l2sq');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1e-6 && errL2 < 1e-5 && errL2sq < 1e-5 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq f16", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-500.0, 500.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec16 = new rampart.vector('f16',tvec);
        var antivec16 = new rampart.vector('f16', antivec);

        var d_same = tvec16.distance(tvec16, 'l2');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = tvec16.distance(antivec16, 'euclidean');
        var d_l2   = tvec16.distance(antivec16, 'l2');
        var d_l2sq = tvec16.distance(antivec16, 'l2sq');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-3;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-2;

        return {
            result : (Math.abs(d_same) < 1e-3 && errL2 < 1e-2 && errL2sq < 1e-2 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq u8", function(){
        var tvec = [], antivec = [];
        for (var i=0; i<1024; i++) {
            var r = rampart.utils.rand(0.0, 1.0);
            tvec.push(r); antivec.push(1.0 - r);
        }
        var tvec8 = new rampart.vector('u8',tvec);
        var antivec8 = new rampart.vector('u8', antivec);

        var d_same = tvec8.distance(tvec8, 'l2');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var scale = 255.0;                       // 0..1 quantized to 0..255
        var exp_l2 = Math.sqrt(sumsq) * scale, exp_l2sq = sumsq * scale * scale;

        var d_eu   = tvec8.distance(antivec8, 'euclidean');
        var d_l2   = tvec8.distance(antivec8, 'l2');
        var d_l2sq = tvec8.distance(antivec8, 'l2sq');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1.0 && errL2 < 0.05 && errL2sq < 0.05 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq i8", function(){
        var tvec = [], antivec = [];
        for (var i=0; i<1024; i++) {
            var r = rampart.utils.rand(-1.0, 1.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec8 = new rampart.vector('i8',tvec);
        var antivec8 = new rampart.vector('i8', antivec);

        var d_same = tvec8.distance(tvec8, 'l2');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var scale = 127.0;                       // -1..1 quantized to -127..127
        var exp_l2 = Math.sqrt(sumsq) * scale, exp_l2sq = sumsq * scale * scale;

        var d_eu   = tvec8.distance(antivec8, 'euclidean');
        var d_l2   = tvec8.distance(antivec8, 'l2');
        var d_l2sq = tvec8.distance(antivec8, 'l2sq');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1.0 && errL2 < 0.05 && errL2sq < 0.05 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    /* ---- split(): recover the k individual vectors from a multi-vector
       value (k*dim cells, e.g. a chunkembed() column) ---- */

    testFeature(pref + "split f32 multi-vector into k parts", function(){
        var flat = [];
        for (var i=0;i<24;i++) flat.push(rampart.utils.rand(-5.0,5.0));
        var v = new rampart.vector('f32', flat);
        var parts = v.split(8);
        if (!Array.isArray(parts) || parts.length != 3) return false;
        var src = v.toNumbers();          // f32-rounded reference
        for (var c=0;c<3;c++) {
            if (parts[c].type !== 'f32' || parts[c].dim !== 8) return false;
            var pn = parts[c].toNumbers();
            for (var i=0;i<8;i++)
                if (pn[i] !== src[c*8+i]) return false;
        }
        return true;
    });

    testFeature(pref + "split f16 parts match flat toNumbers slices", function(){
        var flat = [];
        for (var i=0;i<32;i++) flat.push(rampart.utils.rand(-1.0,1.0));
        var v = new rampart.vector('f16', flat);
        var parts = v.split(16);
        var src = v.toNumbers();
        for (var c=0;c<2;c++) {
            var pn = parts[c].toNumbers();
            for (var i=0;i<16;i++)
                if (pn[i] !== src[c*16+i]) return false;
        }
        return true;
    });

    testFeature(pref + "split with dim == this.dim returns [copy]", function(){
        var v = new rampart.vector('f32', [1,2,3,4]);
        var parts = v.split(4);
        if (parts.length !== 1 || parts[0].dim !== 4) return false;
        var pn = parts[0].toNumbers();
        return pn[0] === 1 && pn[3] === 4;
    });

    testFeature(pref + "split parts are full vectors (distance works)", function(){
        var flat = [];
        for (var i=0;i<128;i++) flat.push(rampart.utils.rand(-1.0,1.0));
        var v = new rampart.vector('f32', flat);
        var parts = v.split(64);
        var dSelf = parts[0].distance(parts[0], 'l2');
        var dOther = parts[0].distance(parts[1], 'l2');
        return Math.abs(dSelf) < 1e-6 && dOther > 0;
    });

    testFeature(pref + "split bad args throw (0, negative, non-divisor)", function(){
        var v = new rampart.vector('f16', 96);   // 96 cells
        var threw = 0;
        try { v.split(0);   } catch(e) { threw++; }
        try { v.split(-8);  } catch(e) { threw++; }
        try { v.split(31);  } catch(e) { threw++; }  // 96 % 31 != 0
        try { v.split(192); } catch(e) { threw++; }  // > dim
        return threw === 4;
    });

    testFeature(pref + "split b8 on byte-aligned dims (and rejects unaligned)", function(){
        var flat = [];
        for (var i=0;i<32;i++) flat.push(i % 3 ? 1.0 : -1.0);
        var b = new rampart.vector('f32', flat).toBit();   // 32-bit b8 vec
        var parts = b.split(16);
        if (parts.length !== 2 || parts[0].type !== 'b8' || parts[0].dim !== 16)
            return false;
        /* parts' bits must equal the flat vector's bit slices */
        var src = b.toNumbers();
        for (var c=0;c<2;c++) {
            var pn = parts[c].toNumbers();
            for (var i=0;i<16;i++)
                if (pn[i] !== src[c*16+i]) return false;
        }
        var threw = 0;
        try { b.split(12); } catch(e) { threw++; }   // not byte-aligned
        return threw === 1;
    });
}

var rawvec = rampart.vector.raw;

function dorawtests(main) {
    var pref="";
    if(!main)
        pref = "thr: ";
    //make a random vector:
    var myvec=[];
    for (var i=0;i<64;i++)
        myvec.push(rampart.utils.rand(-5.0,5.0));

    var mybvec=[];
    for (var i=0;i<64;i++)
        mybvec.push(rampart.utils.rand(-5000.0,5000.0));

    testFeature(pref + "convert number array to f64 and back", function(){
        var v64 = rawvec.numbersToF64(myvec);
        var nvec = rawvec.f64ToNumbers(v64);
        var ret=0.0;

        for (var i=0; i<myvec.length; i++)
            ret += myvec[i] - nvec[i];

        return ret == 0.0;
    });

    testFeature(pref + "convert number array to f32 and back", function(){
        var v32 = rawvec.numbersToF32(myvec);
        var nvec = rawvec.f32ToNumbers(v32);
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);

        return err < 0.00001;
    });

    testFeature(pref + "convert number array to f16 and back", function(){
        var v16 = rawvec.numbersToF16(myvec);
        var nvec = rawvec.f16ToNumbers(v16);
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);
        return err < 0.1;
    });

    testFeature(pref + "convert number array to bf16 and back", function(){
        var b16 = rawvec.numbersToBf16(mybvec);
        var nvec = rawvec.bf16ToNumbers(b16);
        var err=0.0;

        for (var i=0; i<mybvec.length; i++)
        {
            err += Math.abs(mybvec[i] - nvec[i]);
            //printf("%f vs %f\n", mybvec[i], nvec[i]);
        }
        //printf("err=%f\n", err);
        return err < 500.0;
    });

    var vecsz=1024;
    testFeature(pref + "convert nums to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var u8 = rawvec.numbersToU8(tvec);
        var nvec = rawvec.u8ToNumbers(u8);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f64 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var f64 = rawvec.numbersToF64(tvec);
        var u8 = rawvec.f64ToU8(f64);
        var f64out = rawvec.u8ToF64(u8, 1/255);
        var nvec = rawvec.f64ToNumbers(f64out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f32 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var f32 = rawvec.numbersToF32(tvec);
        var u8 = rawvec.f32ToU8(f32);
        var f32out = rawvec.u8ToF32(u8, 1/255);
        var nvec = rawvec.f32ToNumbers(f32out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f16 to u8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(0.0,1.0));
        var f16 = rawvec.numbersToF16(tvec);
        var u8 = rawvec.f16ToU8(f16);
        var f16out = rawvec.u8ToF16(u8, 1/255);
        var nvec = rawvec.f16ToNumbers(f16out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert nums to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var i8 = rawvec.numbersToI8(tvec);
        var nvec = rawvec.i8ToNumbers(i8);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f64 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var f64 = rawvec.numbersToF64(tvec);
        var i8 = rawvec.f64ToI8(f64);
        var f64out = rawvec.i8ToF64(i8, 1/127); // 1/127 is the default scale
        var nvec = rawvec.f64ToNumbers(f64out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f32 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var f32 = rawvec.numbersToF32(tvec);
        var i8 = rawvec.f32ToI8(f32);
        var f32out = rawvec.i8ToF32(i8, 1/127);
        var nvec = rawvec.f32ToNumbers(f32out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "convert f16 to i8 and back", function(){
        var tvec = [];
        for (var i=0;i<vecsz;i++)
            tvec.push(rampart.utils.rand(-1.0,1.0));
        var f16 = rawvec.numbersToF16(tvec);
        var i8 = rawvec.f16ToI8(f16);
        var f16out = rawvec.i8ToF16(i8);
        var nvec = rawvec.f16ToNumbers(f16out);
        var err=0.0;

        for (var i=0; i<tvec.length; i++)
        {
            err += Math.abs(tvec[i] - nvec[i]);
            //printf("%f vs %f   %f\n", tvec[i], nvec[i], (tvec[i] - nvec[i]))
        }
        err/=vecsz;
        return { result: (err < 0.07), text: sprintf("(avgerr=%.4f)", err)};
    });

    testFeature(pref + "normalize Numbers", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        rawvec.l2NormalizeNumbers(tvec);

        for (var i=0; i<64; i++)
            sum += tvec[i] * tvec[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F64", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in64 = rawvec.numbersToF64(tvec);

        rawvec.l2NormalizeF64(in64);

        var outNum = rawvec.f64ToNumbers(in64);

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F32", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in32 = rawvec.numbersToF32(tvec);

        rawvec.l2NormalizeF32(in32);

        var outNum = rawvec.f32ToNumbers(in32);

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F16", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-500.0,500.0));

        var in16 = rawvec.numbersToF16(tvec);

        rawvec.l2NormalizeF16(in16);

        var outNum = rawvec.f16ToNumbers(in16);

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.001 && sum > 0.999;
    });

    testFeature(pref + "distance dot numbers", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        rawvec.l2NormalizeNumbers(tvec);
        rawvec.l2NormalizeNumbers(antivec);

        var score = rawvec.distance(tvec, tvec, 'dot', 'numbers');
        var score2 = rawvec.distance(tvec, antivec, 'dot', 'numbers');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance dot f64", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec64 = rawvec.numbersToF64(tvec);
        var antivec64 = rawvec.numbersToF64(antivec);

        rawvec.l2NormalizeF64(tvec64);
        rawvec.l2NormalizeF64(antivec64);

        var score = rawvec.distance(tvec64, tvec64, 'dot', 'f64');
        var score2 = rawvec.distance(tvec64, antivec64, 'dot', 'f64');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance dot f32", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec32 = rawvec.numbersToF32(tvec);
        var antivec32 = rawvec.numbersToF32(antivec);

        rawvec.l2NormalizeF32(tvec32);
        rawvec.l2NormalizeF32(antivec32);

        var score = rawvec.distance(tvec32, tvec32, 'dot', 'f32');
        var score2 = rawvec.distance(tvec32, antivec32, 'dot', 'f32');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });


    testFeature(pref + "distance dot f16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = rawvec.numbersToF16(tvec);
        var antivec16 = rawvec.numbersToF16(antivec);

        rawvec.l2NormalizeF16(tvec16);
        rawvec.l2NormalizeF16(antivec16);

        var score = rawvec.distance(tvec16, tvec16, 'dot', 'f16');
        var score2 = rawvec.distance(tvec16, antivec16, 'dot', 'f16');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine numbers", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var score = 1 - rawvec.distance(tvec, tvec, 'cosine', 'numbers');
        var score2 = 1 - rawvec.distance(tvec, antivec, 'cosine', 'numbers');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine f64", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec64 = rawvec.numbersToF64(tvec);
        var antivec64 = rawvec.numbersToF64(antivec);

        var score = 1 - rawvec.distance(tvec64, tvec64, 'cosine', 'f64');
        var score2 = 1 - rawvec.distance(tvec64, antivec64, 'cosine', 'f64');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine f32", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec32 = rawvec.numbersToF32(tvec);
        var antivec32 = rawvec.numbersToF32(antivec);

        var score = 1 - rawvec.distance(tvec32, tvec32, 'cosine', 'f32');
        var score2 = 1 - rawvec.distance(tvec32, antivec32, 'cosine', 'f32');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });


    testFeature(pref + "distance cosine f16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = rawvec.numbersToF16(tvec);
        var antivec16 = rawvec.numbersToF16(antivec);

        var score = 1 - rawvec.distance(tvec16, tvec16, 'cosine', 'f16');
        var score2 = 1 - rawvec.distance(tvec16, antivec16, 'cosine', 'f16');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine bf16", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<64;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = rawvec.numbersToBf16(tvec);
        var antivec16 = rawvec.numbersToBf16(antivec);

        var score = 1 - rawvec.distance(tvec16, tvec16, 'cosine', 'bf16');
        var score2 = 1 - rawvec.distance(tvec16, antivec16, 'cosine', 'bf16');

        return score < 1.001 && score > 0.999 && score2 > -1.001 && score2 < -0.999;
    });

    testFeature(pref + "distance cosine u8", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<1024;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = rawvec.numbersToU8(tvec);
        var antivec16 = rawvec.numbersToU8(antivec);

        var score = 1 - rawvec.distance(tvec16, tvec16, 'cosine', 'u8');
        var score2 = 1 - rawvec.distance(tvec16, antivec16, 'cosine', 'u8');
        //printf("scores: %f %f\n", score, score2);

        /* The distance kernel rebases u8 by the symmetric zeroPoint (128)
         * before cosine, so mirror-opposite vectors score ~ -1.0, exactly
         * like i8.  See note on the typed-vector u8 test. */
        return {
            result : (score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8),
            text : sprintf("(180deg err=%.3f)", score2+1.0)
        };
    });

    testFeature(pref + "distance cosine i8", function(){
        var tvec=[];
        var sum=0.0;
        var antivec=[];

        for (var i=0;i<1024;i++)
        {
            var r = rampart.utils.rand(-5000.0,5000.0);
            tvec.push(r);
            antivec.push(-r);
        }

        var tvec16 = rawvec.numbersToI8(tvec);
        var antivec16 = rawvec.numbersToI8(antivec);

        var score = 1 - rawvec.distance(tvec16, tvec16, 'cosine', 'i8');
        var score2 = 1 - rawvec.distance(tvec16, antivec16, 'cosine', 'i8');
        return {
            result : (score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8),
            text : sprintf("(180deg err=%.3f)", score2+1.0)
        };
        return score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8;
    });
    // ------------------------------------------------------------
    // L2 (Euclidean) and L2-squared distance tests (raw form).
    //   'euclidean' / 'l2'      -> true L2 (sqrt of sum of squares)
    //   'l2sq' / 'sqeuclidean'  -> squared L2
    // Each test exercises BOTH metrics and verifies euclidean==l2 (alias)
    // and euclidean^2 == l2sq.
    // ------------------------------------------------------------

    testFeature(pref + "distance l2/l2sq num", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-5000.0, 5000.0);
            tvec.push(r); antivec.push(-r);
        }

        var d_same = rawvec.distance(tvec, tvec, 'l2', 'numbers');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = rawvec.distance(tvec, antivec, 'euclidean', 'numbers');
        var d_l2   = rawvec.distance(tvec, antivec, 'l2', 'numbers');
        var d_l2sq = rawvec.distance(tvec, antivec, 'l2sq', 'numbers');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-9;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-6;

        return {
            result : (Math.abs(d_same) < 1e-9 && errL2 < 1e-6 && errL2sq < 1e-6 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq f64", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-5000.0, 5000.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec64 = rawvec.numbersToF64(tvec);
        var antivec64 = rawvec.numbersToF64(antivec);

        var d_same = rawvec.distance(tvec64, tvec64, 'l2', 'f64');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = rawvec.distance(tvec64, antivec64, 'euclidean', 'f64');
        var d_l2   = rawvec.distance(tvec64, antivec64, 'l2', 'f64');
        var d_l2sq = rawvec.distance(tvec64, antivec64, 'l2sq', 'f64');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-9;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-6;

        return {
            result : (Math.abs(d_same) < 1e-9 && errL2 < 1e-6 && errL2sq < 1e-6 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq f32", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-5000.0, 5000.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec32 = rawvec.numbersToF32(tvec);
        var antivec32 = rawvec.numbersToF32(antivec);

        var d_same = rawvec.distance(tvec32, tvec32, 'l2', 'f32');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = rawvec.distance(tvec32, antivec32, 'euclidean', 'f32');
        var d_l2   = rawvec.distance(tvec32, antivec32, 'l2', 'f32');
        var d_l2sq = rawvec.distance(tvec32, antivec32, 'l2sq', 'f32');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1e-6 && errL2 < 1e-5 && errL2sq < 1e-5 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq f16", function(){
        var tvec = [], antivec = [];
        for (var i=0;i<64;i++){
            var r = rampart.utils.rand(-500.0, 500.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec16 = rawvec.numbersToF16(tvec);
        var antivec16 = rawvec.numbersToF16(antivec);

        var d_same = rawvec.distance(tvec16, tvec16, 'l2', 'f16');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var exp_l2 = Math.sqrt(sumsq), exp_l2sq = sumsq;

        var d_eu   = rawvec.distance(tvec16, antivec16, 'euclidean', 'f16');
        var d_l2   = rawvec.distance(tvec16, antivec16, 'l2', 'f16');
        var d_l2sq = rawvec.distance(tvec16, antivec16, 'l2sq', 'f16');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-3;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-2;

        return {
            result : (Math.abs(d_same) < 1e-3 && errL2 < 1e-2 && errL2sq < 1e-2 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq u8", function(){
        var tvec = [], antivec = [];
        for (var i=0; i<1024; i++) {
            var r = rampart.utils.rand(0.0, 1.0);
            tvec.push(r); antivec.push(1.0 - r);
        }
        var tvec8 = rawvec.numbersToU8(tvec);
        var antivec8 = rawvec.numbersToU8(antivec);

        var d_same = rawvec.distance(tvec8, tvec8, 'l2', 'u8');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var scale = 255.0;
        var exp_l2 = Math.sqrt(sumsq) * scale, exp_l2sq = sumsq * scale * scale;

        var d_eu   = rawvec.distance(tvec8, antivec8, 'euclidean', 'u8');
        var d_l2   = rawvec.distance(tvec8, antivec8, 'l2', 'u8');
        var d_l2sq = rawvec.distance(tvec8, antivec8, 'l2sq', 'u8');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1.0 && errL2 < 0.05 && errL2sq < 0.05 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });

    testFeature(pref + "distance l2/l2sq i8", function(){
        var tvec = [], antivec = [];
        for (var i=0; i<1024; i++) {
            var r = rampart.utils.rand(-1.0, 1.0);
            tvec.push(r); antivec.push(-r);
        }
        var tvec8 = rawvec.numbersToI8(tvec);
        var antivec8 = rawvec.numbersToI8(antivec);

        var d_same = rawvec.distance(tvec8, tvec8, 'l2', 'i8');
        var sumsq = 0.0;
        for (var i=0;i<tvec.length;i++){ var diff = tvec[i]-antivec[i]; sumsq += diff*diff; }
        var scale = 127.5;
        var exp_l2 = Math.sqrt(sumsq) * scale, exp_l2sq = sumsq * scale * scale;

        var d_eu   = rawvec.distance(tvec8, antivec8, 'euclidean', 'i8');
        var d_l2   = rawvec.distance(tvec8, antivec8, 'l2', 'i8');
        var d_l2sq = rawvec.distance(tvec8, antivec8, 'l2sq', 'i8');

        var errL2   = Math.abs(d_eu   - exp_l2)   / Math.max(1, exp_l2);
        var errL2sq = Math.abs(d_l2sq - exp_l2sq) / Math.max(1, exp_l2sq);
        var aliasOk = Math.abs(d_l2 - d_eu) < 1e-6;
        var sqOk    = Math.abs(d_eu*d_eu - d_l2sq) / Math.max(1, d_l2sq) < 1e-4;

        return {
            result : (Math.abs(d_same) < 1.0 && errL2 < 0.05 && errL2sq < 0.05 && aliasOk && sqOk),
            text   : sprintf("(l2Err=%.2e l2sqErr=%.2e)", errL2, errL2sq)
        };
    });
}
load.Sql;
var sql=Sql.connect(tmpdir + "/vdb", true);

function dosqltests(main) {
    var pref="";
    if(!main)
        pref = "thr: ";
    //make a random vector:
    var tvec, antivec, tvecs=[], antivecs=[];
    for(var i=0;i<10;i++)
    {
        tvec=[];
        antivec=[];
        for (var j=0;j<64;j++)
        {
            var r = rampart.utils.rand(-5.0,5.0);
            tvec.push(r);
            antivec.push(-r);
        }
        tvecs.push(tvec);
        antivecs.push(antivec);
    }

    var rv=rampart.vector;

    testFeature(pref + "insert into types f64, f32, f16", function(){
        sql.query("drop table vtmp;");
        sql.exec("create table vtmp (entry int, v1 vecF64, v2 vecF32, v3 vecF16, av1 vecF64, av2 vecF32, av3 vecF16);");

        for(var i=0;i<10;i++) {
            var tv = new rv('f64',tvecs[i]);
            var av = new rv('f64',antivecs[i]);
            av.l2Normalize();
            tv.l2Normalize();
            sql.exec("insert into vtmp values(?,?,?,?,?,?,?)",
                [ i, tv, tv.toF32(), tv.toF16(),
                  av, av.toF32(), av.toF16() ]
            );
        }
        return true;
    });

    testFeature(pref + "compare inserted values", function(){
        var score=0;
        sql.exec("select * from vtmp;",function(row,i) {
            var tv = new rv('f64',tvecs[i]);
            var av = new rv('f64',antivecs[i]);
            av.l2Normalize();
            tv.l2Normalize();
            score += tv.distance(row.v1,'dot');
            score += tv.toF32().distance(row.v2,'dot');
            score += tv.toF16().distance(row.v3,'dot');
            score += av.distance(row.av1,'dot');
            score += av.toF32().distance(row.av2,'dot');
            score += av.toF16().distance(row.av3,'dot');
        });
        var err = 1 - score/60;
        return {
            result : (err>-0.01 && err < 0.01),
            text   : sprintf("(err: %.3e)", err)
        };
    });

    testFeature(pref + "compare inserted values -rawVecs", function(){
        var score=0;
        sql.exec("select * from vtmp;",
          {rawVectors:true}, // return buffer instead of typed vector
          function(row,i) {
            var tv = rawvec.numbersToF64(tvecs[i]);
            var av = rawvec.numbersToF64(antivecs[i]);
            rawvec.l2NormalizeF64(av);
            rawvec.l2NormalizeF64(tv);

            var tv32 = rawvec.numbersToF32(tvecs[i]);
            var av32 = rawvec.numbersToF32(antivecs[i]);
            rawvec.l2NormalizeF32(av32);
            rawvec.l2NormalizeF32(tv32);

            var tv16 = rawvec.numbersToF16(tvecs[i]);
            var av16 = rawvec.numbersToF16(antivecs[i]);
            rawvec.l2NormalizeF16(av16);
            rawvec.l2NormalizeF16(tv16);

            score += rawvec.distance(tv,   row.v1, 'dot', 'f64');
            score += rawvec.distance(tv32, row.v2, 'dot', 'f32');
            score += rawvec.distance(tv16, row.v3, 'dot', 'f16');
            score += rawvec.distance(av,   row.av1, 'dot', 'f64');
            score += rawvec.distance(av32, row.av2, 'dot', 'f32');
            score += rawvec.distance(av16, row.av3, 'dot', 'f16');
        });
        var err = 1 - score/60;
        return {
            result : (err>-0.01 && err < 0.01),
            text   : sprintf("(err: %.3e)", err)
        };

        return {
            result : (score > 59.9 && score < 60.1),
            text   : sprintf("(%.3f ~= 60.0)", score)
        };
    });

    testFeature(pref + "vecdist()", function(){
        var score=0;
        sql.exec("select v1, av1, vecdist(v1, av1, 'dot') score1, vecdist(v2, av2, 'dot') score2, vecdist(v3, av3, 'dot') score3 from vtmp;",
            function(row,i) {
                score+=row.score1 + row.score2 + row.score3;
            }
        );
        score /= -30.0;
        score -= 1.0;
        return {
            result : (score > -0.01 && score < 0.01),
            text   : sprintf("(avgerr=%.3f)", score)
        };
    });

    testFeature(pref + "orderby vecdist()", function(){
        var score=0;
        var v3 = new rv('f64', tvecs[3]);
        v3.l2Normalize();
        // looking for a score of 1.0 for row 3
        var res = sql.exec("select vecdist(v1, ?, 'dot') score, entry from vtmp order by 1 desc;",
            [v3]
        );
        // looking for a score of -1.0 for row 3
        var res2 = sql.exec("select vecdist(av1, ?, 'dot') score, entry from vtmp order by 1;",
            [v3]
        );

        return (
            res && res.rows && res.rows[0] && res.rows[0].entry == 3 &&
            res2 && res2.rows && res2.rows[0] && res2.rows[0].entry == 3
        )
    });

    testFeature(pref + "orderby vecdist() - raw buffer comp", function(){
        var score=0;
        var v3 = new rv('f64', tvecs[4]);
        v3.l2Normalize();
        // looking for a score of 1.0 for row 4
        var res = sql.exec("select vecdist(v1, ?, 'dot') score, entry from vtmp order by 1 desc;",
            [v3.toRaw()]
        );
        // looking for a score of -1.0 for row 4
        var res2 = sql.exec("select vecdist(av1, ?, 'dot') score, entry from vtmp order by 1;",
            [v3.toRaw()]
        );

        return (
            res && res.rows && res.rows[0] && res.rows[0].entry == 4 &&
            res2 && res2.rows && res2.rows[0] && res2.rows[0].entry == 4
        )
    });
}

// typed vector tests
dotests(true);

// raw buffer as vector tests:
ttype = "vecRaw";
dorawtests(true);

// raw buffer as vector tests:
ttype = "vecSql";
dosqltests(true);


shell("rm -rf " + tmpdir + "/vdb");
try { rmdir(tmpdir); } catch(e) {}
testFeature.exit();
// pretty much unnecessary:

// repeat tests in a thread:
var thr = new rampart.thread();
thr.exec(function(){

    ttype = "vector";
    dotests()

    ttype = "vecRaw";
    dorawtests();

});

shell("rm -rf " + tmpdir + "/vdb");
try { rmdir(tmpdir); } catch(e) {}
