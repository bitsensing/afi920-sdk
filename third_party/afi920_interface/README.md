# Vendored AFI920 interface headers

Public SOME/IP and ISO 23150 C headers of the AFI920 interface v1.1.0,
copied from the bitsensing AFI920 interface definitions (commit 21e4ef5).
Only the headers the SDK build includes are vendored; `src/CMakeLists.txt`
adds `third_party/afi920_interface/src/c` to the include path.
