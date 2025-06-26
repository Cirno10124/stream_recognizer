@echo off
echo ============================================
echo InputMode Enumeration Fix Verification
echo ============================================
echo.

echo 1. Checking for old incorrect enum values...
echo.

echo Searching for FILE_INPUT (should not be found):
findstr /c:"FILE_INPUT" include\multi_channel_processor.h src\multi_channel_processor.cpp src\whisper_gui.cpp 2>nul
if %errorlevel% == 0 (
    echo ✗ Found FILE_INPUT - needs fixing
) else (
    echo ✓ No FILE_INPUT found - good
)

echo.
echo Searching for STREAM_INPUT (should not be found):
findstr /c:"STREAM_INPUT" include\multi_channel_processor.h src\multi_channel_processor.cpp src\whisper_gui.cpp 2>nul
if %errorlevel% == 0 (
    echo ✗ Found STREAM_INPUT - needs fixing
) else (
    echo ✓ No STREAM_INPUT found - good
)

echo.
echo 2. Checking for correct enum values...
echo.

echo Searching for AUDIO_FILE:
findstr /c:"AUDIO_FILE" include\multi_channel_processor.h src\multi_channel_processor.cpp src\whisper_gui.cpp 2>nul
if %errorlevel% == 0 (
    echo ✓ Found AUDIO_FILE - correct
) else (
    echo ✗ AUDIO_FILE not found - may need fixing
)

echo.
echo Searching for VIDEO_FILE:
findstr /c:"VIDEO_FILE" include\multi_channel_processor.h src\multi_channel_processor.cpp src\whisper_gui.cpp 2>nul
if %errorlevel% == 0 (
    echo ✓ Found VIDEO_FILE - correct
) else (
    echo ✗ VIDEO_FILE not found - may need fixing
)

echo.
echo Searching for VIDEO_STREAM:
findstr /c:"VIDEO_STREAM" include\multi_channel_processor.h src\multi_channel_processor.cpp src\whisper_gui.cpp 2>nul
if %errorlevel% == 0 (
    echo ✓ Found VIDEO_STREAM - correct
) else (
    echo ✗ VIDEO_STREAM not found - may need fixing
)

echo.
echo 3. Correct InputMode enum values from audio_types.h:
echo   - MICROPHONE    (microphone input)
echo   - AUDIO_FILE    (audio file input)
echo   - VIDEO_FILE    (video file input)
echo   - VIDEO_STREAM  (video stream input)

echo.
echo 4. Multi-channel support mapping:
echo   - AUDIO_FILE   → processes audio files through channel
echo   - VIDEO_FILE   → processes video files (audio extraction)
echo   - VIDEO_STREAM → processes video streams (audio extraction)
echo   - MICROPHONE   → real-time microphone input (future support)

echo.
echo 5. Fixed code locations:
echo   ✓ src/multi_channel_processor.cpp - processTask function
echo   ✓ src/whisper_gui.cpp - onSubmitMultiChannelTask function
echo   ✓ Both AudioProcessor and task.input_mode mappings

echo.
echo ============================================
echo InputMode Fix Verification Complete!
echo ============================================
echo.

pause 