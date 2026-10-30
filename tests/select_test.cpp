// Tiny tests for parse_selection(). Run with: make test
#include <iostream>

#include "select.hpp"

static int failures = 0;

static void expect(const std::string& input, int total, const std::string& want) {
    std::vector<int> got;
    std::string err;
    bool ok = parse_selection(input, total, got, err);
    std::string result = ok ? join_indices(got) : "ERROR";
    bool pass = result == want;
    if (!pass) ++failures;
    std::cout << (pass ? "  ok   " : "  FAIL ") << "\"" << input << "\" (of " << total << ") -> "
              << result << (ok ? "" : "  [" + err + "]")
              << (pass ? "" : "   expected " + want) << "\n";
}

int main() {
    expect("all", 5, "1,2,3,4,5");
    expect("", 3, "1,2,3");
    expect("ALL", 2, "1,2");
    expect("1,3,5", 8, "1,3,5");
    expect("5,3,1,3", 8, "1,3,5");          // sorted + no duplicates
    expect("^5", 8, "1,2,3,4,5");
    expect("5*", 8, "5,6,7,8");
    expect("2-4", 8, "2,3,4");
    expect("1,3,^2,7*", 8, "1,2,3,7,8");
    expect(" 1 , 3  6 ", 8, "1,3,6");        // spaces are fine
    expect("9", 8, "ERROR");                 // out of range
    expect("0", 8, "ERROR");
    expect("^9", 8, "ERROR");
    expect("4-2", 8, "ERROR");               // backwards
    expect("abc", 8, "ERROR");
    expect("*", 8, "ERROR");
    expect("^", 8, "ERROR");

    std::cout << (failures == 0 ? "All tests passed.\n" : "Some tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}
