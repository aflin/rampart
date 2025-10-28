rampart.globalize(rampart.utils);

function testFeature(name,test)
{
    var error=false;

    if (typeof test =='function'){
        try {
            test=test();
        } catch(e) {
            error=e;
            test=false;
        }
    }

    if(getType(test) == 'Object')
    {
        if(typeof test.text == 'string')
            name += ' ' + test.text;
        test = test.result;
    }

    if(test) {
        printf("testing vector - %-51s - passed\n", name);
    } else {
        printf("testing vector - %-51s - >>>>> FAILED <<<<<\n", name);
        if(error) console.log(error);
        process.exit(1);
    }
    if(error) console.log(error);
}

// less typing
var vec = rampart.vector;

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
        var v64 = vec.numbersToF64(myvec);
        var nvec = vec.f64ToNumbers(v64);
        var ret=0.0;

        for (var i=0; i<myvec.length; i++)
            ret += myvec[i] - nvec[i];

        return ret == 0.0;
    });

    testFeature(pref + "convert number array to f32 and back", function(){
        var v32 = vec.numbersToF32(myvec);
        var nvec = vec.f32ToNumbers(v32);
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);

        return err < 0.00001;
    });

    testFeature(pref + "convert number array to f16 and back", function(){
        var v16 = vec.numbersToF16(myvec);
        var nvec = vec.f16ToNumbers(v16);
        var err=0.0;

        for (var i=0; i<myvec.length; i++)
            err += Math.abs(myvec[i] - nvec[i]);
        return err < 0.1;
    });

    testFeature(pref + "convert number array to bf16 and back", function(){
        var b16 = vec.numbersToBf16(mybvec);
        var nvec = vec.bf16ToNumbers(b16);
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
        var u8 = vec.numbersToU8(tvec);
        var nvec = vec.u8ToNumbers(u8);
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
        var f64 = vec.numbersToF64(tvec);
        var u8 = vec.f64ToU8(f64);
        var f64out = vec.u8ToF64(u8, 1/255);
        var nvec = vec.f64ToNumbers(f64out);
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
        var f32 = vec.numbersToF32(tvec);
        var u8 = vec.f32ToU8(f32);
        var f32out = vec.u8ToF32(u8, 1/255);
        var nvec = vec.f32ToNumbers(f32out);
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
        var f16 = vec.numbersToF16(tvec);
        var u8 = vec.f16ToU8(f16);
        var f16out = vec.u8ToF16(u8, 1/255);
        var nvec = vec.f16ToNumbers(f16out);
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
        var i8 = vec.numbersToI8(tvec);
        var nvec = vec.i8ToNumbers(i8);
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
        var f64 = vec.numbersToF64(tvec);
        var i8 = vec.f64ToI8(f64);
        var f64out = vec.i8ToF64(i8, 1/127.5); // 1/127.5 is the default scale
        var nvec = vec.f64ToNumbers(f64out);
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
        var f32 = vec.numbersToF32(tvec);
        var i8 = vec.f32ToI8(f32);
        var f32out = vec.i8ToF32(i8, 1/127.5);
        var nvec = vec.f32ToNumbers(f32out);
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
        var f16 = vec.numbersToF16(tvec);
        var i8 = vec.f16ToI8(f16);
        var f16out = vec.i8ToF16(i8);
        var nvec = vec.f16ToNumbers(f16out);
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

        vec.l2NormalizeNumbers(tvec);

        for (var i=0; i<64; i++)
            sum += tvec[i] * tvec[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F64", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in64 = vec.numbersToF64(tvec);

        vec.l2NormalizeF64(in64);

        var outNum = vec.f64ToNumbers(in64);

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F32", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-5000.0,5000.0));

        var in32 = vec.numbersToF32(tvec);

        vec.l2NormalizeF32(in32);

        var outNum = vec.f32ToNumbers(in32);

        for (var i=0; i<64; i++)
            sum += outNum[i] * outNum[i];

        return sum < 1.00001 && sum > 0.99999;
    });

    testFeature(pref + "normalize F16", function(){
        var tvec=[];
        var sum=0.0;

        for (var i=0;i<64;i++)
            tvec.push(rampart.utils.rand(-500.0,500.0));

        var in16 = vec.numbersToF16(tvec);

        vec.l2NormalizeF16(in16);

        var outNum = vec.f16ToNumbers(in16);

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

        vec.l2NormalizeNumbers(tvec);
        vec.l2NormalizeNumbers(antivec);

        var score = vec.distance(tvec, tvec, 'dot', 'numbers');
        var score2 = vec.distance(tvec, antivec, 'dot', 'numbers');

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

        var tvec64 = vec.numbersToF64(tvec);
        var antivec64 = vec.numbersToF64(antivec);

        vec.l2NormalizeF64(tvec64);
        vec.l2NormalizeF64(antivec64);

        var score = vec.distance(tvec64, tvec64, 'dot', 'f64');
        var score2 = vec.distance(tvec64, antivec64, 'dot', 'f64');

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

        var tvec32 = vec.numbersToF32(tvec);
        var antivec32 = vec.numbersToF32(antivec);

        vec.l2NormalizeF32(tvec32);
        vec.l2NormalizeF32(antivec32);

        var score = vec.distance(tvec32, tvec32, 'dot', 'f32');
        var score2 = vec.distance(tvec32, antivec32, 'dot', 'f32');

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

        var tvec16 = vec.numbersToF16(tvec);
        var antivec16 = vec.numbersToF16(antivec);

        vec.l2NormalizeF16(tvec16);
        vec.l2NormalizeF16(antivec16);

        var score = vec.distance(tvec16, tvec16, 'dot', 'f16');
        var score2 = vec.distance(tvec16, antivec16, 'dot', 'f16');

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

        var score = 1 - vec.distance(tvec, tvec, 'cosine', 'numbers');
        var score2 = 1 - vec.distance(tvec, antivec, 'cosine', 'numbers');

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

        var tvec64 = vec.numbersToF64(tvec);
        var antivec64 = vec.numbersToF64(antivec);

        var score = 1 - vec.distance(tvec64, tvec64, 'cosine', 'f64');
        var score2 = 1 - vec.distance(tvec64, antivec64, 'cosine', 'f64');

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

        var tvec32 = vec.numbersToF32(tvec);
        var antivec32 = vec.numbersToF32(antivec);

        var score = 1 - vec.distance(tvec32, tvec32, 'cosine', 'f32');
        var score2 = 1 - vec.distance(tvec32, antivec32, 'cosine', 'f32');

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

        var tvec16 = vec.numbersToF16(tvec);
        var antivec16 = vec.numbersToF16(antivec);

        var score = 1 - vec.distance(tvec16, tvec16, 'cosine', 'f16');
        var score2 = 1 - vec.distance(tvec16, antivec16, 'cosine', 'f16');

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

        var tvec16 = vec.numbersToBf16(tvec);
        var antivec16 = vec.numbersToBf16(antivec);

        var score = 1 - vec.distance(tvec16, tvec16, 'cosine', 'bf16');
        var score2 = 1 - vec.distance(tvec16, antivec16, 'cosine', 'bf16');

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

        var tvec16 = vec.numbersToU8(tvec);
        var antivec16 = vec.numbersToU8(antivec);

        var score = 1 - vec.distance(tvec16, tvec16, 'cosine', 'u8');
        var score2 = 1 - vec.distance(tvec16, antivec16, 'cosine', 'u8');
        return {
            result : (score < 1.001 && score > 0.999 && score2 > 0.0 && score2 < 0.1),
            text : sprintf("( 90deg err=%.3f)", score2)
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

        var tvec16 = vec.numbersToI8(tvec);
        var antivec16 = vec.numbersToI8(antivec);

        var score = 1 - vec.distance(tvec16, tvec16, 'cosine', 'i8');
        var score2 = 1 - vec.distance(tvec16, antivec16, 'cosine', 'i8');
        return {
            result : (score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8),
            text : sprintf("(180deg err=%.3f)", score2+1.0)
        };
        return score < 1.001 && score > 0.999 && score2 > -1.2 && score2 < -0.8;
    });

}

dotests(true);

var thr = new rampart.thread();
thr.exec(dotests);
