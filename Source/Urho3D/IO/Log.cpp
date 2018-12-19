//
// Copyright (c) 2008-2018 the Urho3D project.
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
#include "../Core/ProcessUtils.h"
#include "../Core/Profiler.h"
#include "../Core/Thread.h"
#include "../Core/Timer.h"
#include "../IO/File.h"
#include "../IO/IOEvents.h"
#include "../IO/Log.h"

#include <cstdio>

#ifdef __ANDROID__
#include <android/log.h>
#endif
#if defined(IOS) || defined(TVOS)
extern "C" void SDL_IOS_LogMessage(const char* message);
#endif

#include "../DebugNew.h"

namespace Urho3D
{

const char* logLevelPrefixes[] =
{
    "TRACE",
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR",
    nullptr
};

static Log* logInstance = nullptr;
static bool threadErrorDisplayed = false;

Log::Log(Context* context) :
    Object(context),
#ifdef _DEBUG
    level_(LOG_DEBUG),
#else
    level_(LOG_INFO),
#endif
    timeStampFormat_(DEFAULT_DATE_TIME_FORMAT),
    inWrite_(false),
    quiet_(false)
{
    logInstance = this;

    SubscribeToEvent(E_ENDFRAME, URHO3D_HANDLER(Log, HandleEndFrame));
}

Log::~Log()
{
    logInstance = nullptr;
}

void Log::Open(const String& fileName)
{
#if !defined(__ANDROID__) && !defined(IOS) && !defined(TVOS)
    if (fileName.Empty())
        return;

    if (fileName == NULL_DEVICE)
        return;

    if (logFile_ && logFile_->IsOpen())
    {
        if (logFile_->GetName() == fileName)
            return;
        else
            Close();
    }

    logFile_ = new File(context_);
    if (logFile_->Open(fileName, FILE_WRITE))
        Write(LOG_INFO, "Opened log file " + fileName);
    else
    {
        logFile_.Reset();
        Write(LOG_ERROR, "Failed to create log file " + fileName);
    }
#endif
}

void Log::Close()
{
#if !defined(__ANDROID__) && !defined(IOS) && !defined(TVOS)
    if (logFile_ && logFile_->IsOpen())
    {
        logFile_->Close();
        logFile_.Reset();
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
}

void Log::SetQuiet(bool quiet)
{
    quiet_ = quiet;
}

void Log::Write(LogLevel level, const String& message)
{
    // Special case for LOG_RAW level
    if (level == LOG_RAW)
    {
        WriteRaw(message, false);
        return;
    }

    // No-op if illegal level
    if (level < LOG_TRACE || level >= LOG_NONE)
        return;

    // If not in the main thread, store message for later processing
    if (!Thread::IsMainThread())
    {
        if (logInstance)
        {
            MutexLock lock(logInstance->logMutex_);
            logInstance->threadMessages_.Push(StoredLogMessage(message, level, false));
        }

        return;
    }

    // Do not log if message level excluded or if currently sending a log event
    if (!logInstance || logInstance->level_ > level || logInstance->inWrite_)
        return;

    String formattedMessage = logLevelPrefixes[level];
    formattedMessage += ": ";
    formattedMessage += String(' ', 7 - formattedMessage.Length());
    formattedMessage += message;
    logInstance->lastMessage_ = message;

    if (!logInstance->timeStampFormat_.Empty())
        formattedMessage = "[" + Time::GetTimeStamp(logInstance->timeStampFormat_) + "] " + formattedMessage;

    URHO3D_PROFILE_MESSAGE(formattedMessage.CString(), formattedMessage.Length());

#if defined(__ANDROID__)
    int androidLevel = ANDROID_LOG_VERBOSE + level;
    __android_log_print(androidLevel, "Urho3D", "%s", message.CString());
#elif defined(IOS) || defined(TVOS)
    SDL_IOS_LogMessage(message.CString());
#else
    if (logInstance->quiet_)
    {
        // If in quiet mode, still print the error message to the standard error stream
        if (level == LOG_ERROR)
            PrintUnicodeLine(formattedMessage, true);
    }
    else
        PrintUnicodeLine(formattedMessage, level == LOG_ERROR);
#endif

    if (logInstance->logFile_)
    {
        logInstance->logFile_->WriteLine(formattedMessage);
        logInstance->logFile_->Flush();
    }

    logInstance->inWrite_ = true;

    using namespace LogMessage;

    VariantMap& eventData = logInstance->GetEventDataMap();
    eventData[P_MESSAGE] = formattedMessage;
    eventData[P_LEVEL] = level;
    logInstance->SendEvent(E_LOGMESSAGE, eventData);

    logInstance->inWrite_ = false;
}

void Log::WriteRaw(const String& message, bool error)
{
    // If not in the main thread, store message for later processing
    if (!Thread::IsMainThread())
    {
        if (logInstance)
        {
            MutexLock lock(logInstance->logMutex_);
            logInstance->threadMessages_.Push(StoredLogMessage(message, LOG_RAW, error));
        }

        return;
    }

    // Prevent recursion during log event
    if (!logInstance || logInstance->inWrite_)
        return;

    logInstance->lastMessage_ = message;

#if defined(__ANDROID__)
    if (logInstance->quiet_)
    {
        if (error)
            __android_log_print(ANDROID_LOG_ERROR, "Urho3D", "%s", message.CString());
    }
    else
        __android_log_print(error ? ANDROID_LOG_ERROR : ANDROID_LOG_INFO, "Urho3D", "%s", message.CString());
#elif defined(IOS) || defined(TVOS)
    SDL_IOS_LogMessage(message.CString());
#else
    if (logInstance->quiet_)
    {
        // If in quiet mode, still print the error message to the standard error stream
        if (error)
            PrintUnicode(message, true);
    }
    else
        PrintUnicode(message, error);
#endif

    if (logInstance->logFile_)
    {
        logInstance->logFile_->Write(message.CString(), message.Length());
        logInstance->logFile_->Flush();
    }

    logInstance->inWrite_ = true;

    using namespace LogMessage;

    VariantMap& eventData = logInstance->GetEventDataMap();
    eventData[P_MESSAGE] = message;
    eventData[P_LEVEL] = error ? LOG_ERROR : LOG_INFO;
    logInstance->SendEvent(E_LOGMESSAGE, eventData);

    logInstance->inWrite_ = false;
}

void Log::HandleEndFrame(StringHash eventType, VariantMap& eventData)
{
    // If the MainThreadID is not valid, processing this loop can potentially be endless
    if (!Thread::IsMainThread())
    {
        if (!threadErrorDisplayed)
        {
            fprintf(stderr, "Thread::mainThreadID is not setup correctly! Threaded log handling disabled\n");
            threadErrorDisplayed = true;
        }
        return;
    }

    MutexLock lock(logMutex_);

    // Process messages accumulated from other threads (if any)
    while (!threadMessages_.Empty())
    {
        const StoredLogMessage& stored = threadMessages_.Front();

        if (stored.level_ != LOG_RAW)
            Write(stored.level_, stored.message_);
        else
            WriteRaw(stored.message_, stored.error_);

        threadMessages_.PopFront();
    }
}

}
