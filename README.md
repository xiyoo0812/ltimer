# ltimer
一个给lua使用的时间库，包括常用的时间函数以及高性能定时器！

# 依赖
- [lua](https://github.com/xiyoo0812/lua.git)5.2以上
- 项目路径如下<br>
  |--proj <br>
  &emsp;|--lua <br>
  &emsp;|--ltimer
- [luaext](https://github.com/xiyoo0812/luaext.git) 集成了所有lua扩展库，建议使用或者参考。

# 编译
- msvc: 准备好lua依赖库并放到指定位置，将proj文件加到sln后编译。
- linux: 准备好lua依赖库并放到指定位置，执行make -f ltimer.mak

# 用法