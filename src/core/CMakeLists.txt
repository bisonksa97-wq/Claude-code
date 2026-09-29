up_add_module(up_core
    SOURCES
        AtomicFile.cpp
        Command.cpp
        CommandStack.cpp
        Id.cpp
        Log.cpp
        Rational.cpp
        Result.cpp
        Timecode.cpp
    DEPS Threads::Threads)
