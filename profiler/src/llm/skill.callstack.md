### Inspecting callstacks

1. Focus on user's code. Ignore standard library boilerplate.
2. Retrieve source code to verify callstack validity. Source locations in callstacks are return locations, and the call site may actually be near the reported source line.
3. Top of the callstack is the most interesting, as it shows what the program is doing *now*. The bottom of the callstack shows what the program did to do what it's doing.
4. If the callstack contains Tracy's crash handler, the profiled program has crashed. In this case, ignore the crash handler and any functions it may be calling. The crash happened *before* the handler intercepted it.
