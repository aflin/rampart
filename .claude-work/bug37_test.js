rampart.globalize(rampart.utils);

// Test 1: Duplicate headers with returnType "object" — should crash (NULL deref in closecsv macro)
printf("Test 1: duplicate headers crash...\n");
try {
    var res = rampart.import.csv("name,age,name\nAlice,30,Bob\n", {
        hasHeaderRow: true,
        returnType: "object"
    });
    printf("  No error (unexpected)\n");
} catch(e) {
    printf("  Caught exception (good): %s\n", e.message || e);
}

// Test 2: Bad option type leaks csv — passing string instead of boolean
printf("Test 2: bad boolean option (leaks csv)...\n");
try {
    var res = rampart.import.csv("a,b,c\n1,2,3\n", {
        stripLeadingWhite: "notabool"
    });
    printf("  No error (unexpected)\n");
} catch(e) {
    printf("  Caught exception (good): %s\n", e.message || e);
}

// Test 3: Bad delimiter type leaks csv
printf("Test 3: bad delimiter option (leaks csv)...\n");
try {
    var res = rampart.import.csv("a,b,c\n1,2,3\n", {
        delimiter: 42
    });
    printf("  No error (unexpected)\n");
} catch(e) {
    printf("  Caught exception (good): %s\n", e.message || e);
}

printf("Done — if you got here, no crash.\n");
