# Call stacks

A call stack is a trace of program execution at the moment of capture. This capture can happen at any time, but it usually occurs when a program crashes, so the developer can trace the path that led to the failure. The case of call stacks in a profiler is very different. During profiling, the operating system halts execution of the program at a predefined rate-for example, 10 kHz-notes where the program execution is and the function calls that led it there, then resumes the program. As can be seen, the call stacks in a profiler are slices of a normal workflow you can use to explore execution characteristics, not indications of failure.

# Call stack structure

The top call stack frame is numbered 0, and it is the frame that was executing during the stack capture. Stack frame numbers increase the farther toward the origin function we go. This is the usual convention.

In Tracy Profiler, each execution frame can have multiple *inline* frames. When a program is compiled, the compiler, as an optimization, may inline function calls into the base function ("symbol"), and the profiler can track that.

To show how it works, let's consider the following source code:

```c++
float Square(float val) { return val*val; }
float Distance(Point p1, Point p2) { return sqrt(Square(p1.x-p2.x)+Square(p1.y-p2.y)); }
bool CanReach(Player p, Item i) { return Distance(p.pos, i.pos)<5; }
```

Now, let's say we capture a call stack inside the `Square` function. This is how the call stack can look:

```callstack
0. Square() [inline 0]
0. Distance() [inline 1]
0. CanReach() [inline 2]
1. ItemsLoop()
2. PlayerLogic()
3. ...
```

There are three frames with index 0, which means that both `Square` and `Distance` have been inlined into the `CanReach` function, forming a symbol named `CanReach`. Following the inline stack frame indices, we can also see that the call order is `CanReach` -> `Distance` -> `Square`, which matches what the source code does.

Note that while the example is at the top level, inline frames can appear at any depth of the call stack.

# Important nuance

You need to be very careful when reading call stacks. The usual notion is that call stacks (as the name suggests) show function call stacks, that is, which function called which to get where we are. Unfortunately, this is not true. In reality, call stacks are *function return stacks*. The call stack shows where each function will **return**, not from where it was called.

To fully understand how this works, consider the following source code:

```c++
int main()
{
    auto app = std::make_unique<Application>();
    app->Run();
    app.reset();
}
```

Let's assume the `Application` instance (`app`) is already created and we have entered the `Run` method, where, somewhere inside, we're capturing a call stack. Here's a result we might get:

```callstack
0. ...
1. ...
2. Application::Run()
3. std::unique_ptr<Application>::reset()
4. main()
```

At the first glance it may look like `unique_ptr::reset` was the *call site* of the `Application::Run`, which would make no sense, but this is not the case here. When you remember these are the *function return points*, it becomes much more clear what is happening. As an optimization, `Application::Run` is returning directly into `unique_ptr::reset`, skipping the return to `main` and an unnecessary `reset` function call.

# Crash handler

Tracy Profiler can intercept crashes and report them to the user for analysis. To do this, some code machinery is needed, and then the Tracy crash handler needs to run, capture the call stack, and send it over the network. All this only happens after the actual crash occurred; otherwise, there would be no reason to run the crash handler. As a consequence, the retrieved crash trace may include parts of the crash handler stack, which you must ignore.

# Inspecting call stacks

1. Focus on user's code. Ignore standard library boilerplate.
2. Retrieve source code to verify call stack validity. Source locations in call stacks are return locations, and the call site may actually be near the reported source line.
3. Top of the call stack is the most interesting, as it shows what the program is doing *now*. The bottom of the call stack shows what the program did to do what it's doing.
4. If the call stack contains Tracy's crash handler, the profiled program has crashed. In this case, ignore the crash handler and any functions it may be calling. The crash happened *before* the handler intercepted it.
