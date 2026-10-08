// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "MatrixConfig.hpp"
CMatrixConfig g_Config;
void fail() { MG_CHECK(false); }
constexpr tests::Case cases[] = {{"probe.fail", fail}};
int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
