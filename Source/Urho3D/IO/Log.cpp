//
// Copyright (c) 2008-2020 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Precompiled.h"

#include "../Core/Context.h"
#include "../Core/CoreEvents.h"
#include "../Core/Thread.h"
#include "../Core/Timer.h"
#include "../IO/IOEvents.h"
#include "../IO/Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/base_sink.h>
#if DESKTOP
#include <spdlog/sinks/stdout_color_sinks.h>
#else
#include <spdlog/sinks/stdout_sinks.h>
#endif
#include <spdlog/details/null_mutex.h>

#include <mutex>
#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#include <spdlog/sinks/android_sink.h>
#endif
#if defined(IOS) || defined(TVOS)
extern "C" void SDL_IOS_LogMessage(const char* message);
#endif

#include "../DebugNew.h"


namespace Urho3D
{

static Log* GetLog()
{
    auto* context = Context::GetInstance();
    auto* logInstance = context ? context->GetSubsystem<Log>() : nullptr;
    return logInstance;
}

unsigned FindLastNewlineInRange(const ea::string& str, unsigned position, unsigned count)
{
    const char symbols[] = { '\n' };
    return ea::find_last_of(str.begin() + position, str.begin() + position + count,
        ea::begin(symbols), ea::end(symbols)) - str.begin();
}

ea::vector<ea::string> SliceTextByNewline(const ea::string& str, unsigned maxChunkSize)
{
    if (str.size() <= maxChunkSize)
        return { str };

    ea::vector<ea::string> result;

    unsigned startPosition = 0;
    while (startPosition < str.size())
    {
        const unsigned maxSize = ea::min<unsigned>(str.size() - startPosition, maxChunkSize);
        const unsigned sliceIndex = startPosition + maxSize != str.size()
            ? FindLastNewlineInRange(str, startPosition, maxSize) : str.size() - 1;
        const unsigned chunkSize = sliceIndex != ea::string::npos
            ? sliceIndex - startPosition + 1 : maxSize;

        result.push_back(str.substr(startPosition, chunkSize));
        startPosition += chunkSize;
    }
    return result;
}

#if defined(IOS) || defined(TVOS)
template<typename Mutex>
class IOSSink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        SDL_IOS_LogMessage(ea::string(msg.payload.begin(), msg.payload.end()).c_str());
    }

    void flush_() override { }
};

using IOSSink_mt = IOSSink<std::mutex>;
using IOSSink_st = IOSSink<spdlog::details::null_mutex>;
#endif

static spdlog::level::level_enum ConvertLogLevel(LogLevel level)
{
    switch (level)
    {
    case LOG_TRACE:
        return spdlog::level::trace;
    case LOG_DEBUG:
        return spdlog::level::debug;
    case LOG_INFO:
        return spdlog::level::info;
    case LOG_WARNING:
        return spdlog::level::warn;
    case LOG_ERROR:
        return spdlog::level::err;
    case LOG_NONE:
        return spdlog::level::off;
    default:
        assert(false);
        return spdlog::level::off;
    }
}

static LogLevel ConvertLogLevel(spdlog::level::level_enum level)
{
    switch (level)
    {
    case spdlog::level::trace:
        return LOG_TRACE;
    case spdlog::level::debug:
        return LOG_DEBUG;
    case spdlog::level::info:
        return LOG_INFO;
    case spdlog::level::warn:
        return LOG_WARNING;
    case spdlog::level::err:
        return LOG_ERROR;
    case spdlog::level::off:
        return LOG_NONE;
    default:
        assert(false);
        return LOG_NONE;
    }
}

template<typename Mutex>
class MessageForwarderSink : public spdlog::sinks::base_sink<Mutex>
{
protected:
    void sink_it_(const spdlog::details::log_msg& msg) override
    {
        auto* logInstance = GetLog();
        if (logInstance == nullptr)
            return;
        time_t time = std::chrono::system_clock::to_time_t(msg.time);
        logInstance->SendMessageEvent(ConvertLogLevel(msg.level), time, msg.logger_name.data(), ea::string(msg.payload.data(),
            static_cast<unsigned int>(msg.payload.size())));
    }

    void flush_() override { }
};

using MessageForwarderSink_mt = MessageForwarderSink<std::mutex>;
using MessageForwarderSink_st = MessageForwarderSink<spdlog::details::null_mutex>;

Logger::Logger(void* logger)
    : logger_(logger)
{
}

void Logger::Write(LogLevel level, const ea::string& message) const
{
    if (logger_ == nullptr)
        return;

    auto* logger = reinterpret_cast<spdlog::logger*>(logger_);

    switch (level)
    {
    case LOG_TRACE:
        logger->trace(message.c_str());
        break;
    case LOG_DEBUG:
        logger->debug(message.c_str());
        break;
    case LOG_INFO:
        logger->info(message.c_str());
        break;
    case LOG_WARNING:
        logger->warn(message.c_str());
        break;
    case LOG_ERROR:
        logger->error(message.c_str());
        break;
    case LOG_NONE:
    case MAX_LOGLEVELS:
        logger->warn("(Unknown log level used!) %s", message.c_str());
        break;
    }
}

class LogImpl : public Object
{
    URHO3D_OBJECT(LogImpl, Object);
public:
    explicit LogImpl(Context* context) : Object(context)
    {
        sinkProxy_ = std::make_shared<spdlog::sinks::dist_sink_mt>();
#if defined(__ANDROID__)
        platformSink_ = std::make_shared<spdlog::sinks::android_sink_mt>("Urho3D");
#elif defined(IOS) || defined(TVOS)
        platformSink_ = std::make_shared<IOSSink_mt>();
#elif defined(DESKTOP)
#ifdef _WIN32
        platformSink_ = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
#else
        platformSink_ = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
#endif
#else   // Non-desktop platforms like WEB/UWP.
        platformSink_ = std::make_shared<spdlog::sinks::stdout_sink_mt>();
#endif
        sinkProxy_->add_sink(platformSink_);
        sinkProxy_->add_sink(std::make_shared<MessageForwarderSink_mt>());
    }

#ifdef __ANDROID__
    /// Android adb logcat sink
    std::shared_ptr<spdlog::sinks::android_sink_mt> platformSink_;
#elif defined(IOS) || defined(TVOS)
    /// IOS sink.
    std::shared_ptr<IOSSink_mt> platformSink_;
#elif defined(DESKTOP)
    /// File sink. Only for desktops.
    std::shared_ptr<spdlog::sinks::basic_file_sink_mt> fileSink_;
    /// STDOUT sink.
#ifdef _WIN32
    std::shared_ptr<spdlog::sinks::wincolor_stdout_sink_mt> platformSink_;
#else
    std::shared_ptr<spdlog::sinks::ansicolor_stdout_sink_mt> platformSink_;
#endif
#else   // Non-desktop platforms like WEB/UWP.
    std::shared_ptr<spdlog::sinks::stdout_sink_mt> platformSink_;
#endif  // defined(IOS) || defined(TVOS)
    /// Sink that forwards messages to all other sinks.
    std::shared_ptr<spdlog::sinks::dist_sink_mt> sinkProxy_;
};

Log::Log(Context* context) :
    Object(context),
    impl_(new LogImpl(context)),
    formatPattern_("[%H:%M:%S] [%l] [%n] : %v")
{
#if !__EMSCRIPTEN__
    spdlog::flush_every(std::chrono::seconds(5));
#endif
    SubscribeToEvent(E_ENDFRAME, URHO3D_HANDLER(Log, HandleEndFrame));
}

Log::~Log()
{
    spdlog::shutdown();
}

void Log::Open(const ea::string& fileName)
{
#if defined(DESKTOP)
    if (fileName.empty())
        return;

    if (fileName == NULL_DEVICE)
        return;

    Close();

    impl_->fileSink_ = std::make_shared<spdlog::sinks::basic_file_sink_mt>(fileName.c_str());
    impl_->fileSink_->set_pattern(formatPattern_.c_str());
    impl_->sinkProxy_->add_sink(impl_->fileSink_);
#endif
}

void Log::Close()
{
#if defined(DESKTOP)
    if (impl_->fileSink_)
    {
        impl_->sinkProxy_->remove_sink(impl_->fileSink_);
        impl_->fileSink_ = nullptr;
    }
#endif
}

void Log::SetLevel(LogLevel level)
{
    if (level < LOG_TRACE || level > LOG_NONE)
    {
        URHO3D_LOGERRORF("Attempted to set erroneous log level %d", level);
        return;
    }

    level_ = level;
    spdlog::set_level(ConvertLogLevel(level));
}

void Log::SetQuiet(bool quiet)
{
    quiet_ = quiet;
    impl_->platformSink_->set_level(ConvertLogLevel(quiet ? LOG_NONE : level_));
}

void Log::SetLogFormat(const ea::string& format)
{
    formatPattern_ = format;

    if (impl_->platformSink_)
        impl_->platformSink_->set_pattern(format.c_str());

#if defined(DESKTOP)
    // May not be opened yet if patter is set from Application::Setup().
    if (impl_->fileSink_)
        impl_->fileSink_->set_pattern(format.c_str());
#endif
}

Logger Log::GetLogger(const ea::string& name)
{
    // Loggers may be used only after initializing Log subsystem, therefore do not use logging from static initializers.
    auto* logInstance = GetLog();
    if (logInstance == nullptr)
        return {};

    std::shared_ptr<spdlog::logger> logger;
    MutexLock lock(logInstance->logMutex_);

    logger = spdlog::get(name);

    if (!logger)
    {
        logger = std::make_shared<spdlog::logger>(name, logInstance->impl_->sinkProxy_);
        spdlog::register_logger(logger);
    }

    return Logger(reinterpret_cast<void*>(logger.get()));
}

Logger Log::GetLogger()
{
    auto* logInstance = GetLog();
    if (logInstance == nullptr)
        return {};
    static Logger defaultLogger = Log::GetLogger("main");
    return defaultLogger;
}

void Log::SendMessageEvent(LogLevel level, time_t timestamp, const ea::string& logger, const ea::string& message)
{
    // No-op if illegal level
    if (level < LOG_TRACE || level >= LOG_NONE)
        return;

#if URHO3D_PROFILING
    const unsigned maxMessageLength = std::numeric_limits<uint16_t>::max() - 1;
    if (message.size() <= maxMessageLength)
    {
        TracyMessageC(message.c_str(), message.size(), LOG_LEVEL_COLORS[level].ToUIntArgb());
    }
    else
    {
        const auto chunks = SliceTextByNewline(message, maxMessageLength);
        for (const ea::string& chunk : chunks)
            TracyMessageC(chunk.c_str(), chunk.size(), LOG_LEVEL_COLORS[level].ToUIntArgb());
    }
#endif

    auto* logInstance = GetLog();
    if (logInstance == nullptr)
        return;

    // If not in the main thread, store message for later processing
    if (!Thread::IsMainThread())
    {
        MutexLock lock(logMutex_);
        threadMessages_.push_back(StoredLogMessage(level, timestamp, logger, message));
        return;
    }

    if (inWrite_)
        return;

    inWrite_ = true;

    using namespace LogMessage;

    VariantMap& eventData = logInstance->GetEventDataMap();
    eventData[P_LEVEL] = level;
    eventData[P_TIME] = (unsigned)timestamp;
    eventData[P_LOGGER] = logger;
    eventData[P_MESSAGE] = message;
    SendEvent(E_LOGMESSAGE, eventData);

    inWrite_ = false;
}

void Log::PumpThreadMessages()
{
    // If the MainThreadID is not valid, processing this loop can potentially be endless
    if (!Thread::IsMainThread())
    {
        static bool threadErrorDisplayed = false;
        if (!threadErrorDisplayed)
        {
            fprintf(stderr, "Thread::mainThreadID is not setup correctly! Threaded log handling disabled\n");
            threadErrorDisplayed = true;
        }
        return;
    }

    MutexLock lock(logMutex_);

    // Process messages accumulated from other threads (if any)
    while (!threadMessages_.empty())
    {
        const StoredLogMessage& stored = threadMessages_.front();
        SendMessageEvent(stored.level_, stored.timestamp_, stored.logger_, stored.message_);
        threadMessages_.pop_front();
    }
}

}
