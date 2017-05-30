# lj_strext
A string and json encode/decode library implemented by C extend for luajit
 
本库中包含了一份Json编码解码的代码，经过一段时间的测试，应该是最快的基于Lua的最快的Json编码库了，速度快过cJSON，某些情况下甚至快出4倍以上。
另外本库中还有一堆为string库扩展的函数，是我平时常用的，各位看官喜欢就继续使用，不喜欢没有关系。
 
本库在VC下编写，带有VC工程，也可以Linux下编译，不过没写编译脚本，用CMake把所有的CPP文件加上就可以编译出来
VC下默认编译出来的文件名为strext.dll。使用的时候，直接用luajit启动，require('strext')就可以了
