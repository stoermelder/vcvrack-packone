@echo off
rem Generate a synthetic dataset, train a tiny model, and emit a C++ header
rem fragment you paste into src/modules/Siren/SirenTagClassifier.cpp.
rem
rem Usage:
rem   scripts\siren-tag-model\run.bat
rem   scripts\siren-tag-model\run.bat --n-per-class 200
rem   scripts\siren-tag-model\run.bat --csv my_real_dataset.csv
rem
rem After it finishes, the generated body is in:
rem   scripts\siren-tag-model\build\SirenTagClassifier.generated.cpp
rem Copy it into the marked region of the C++ header and rebuild.

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
rem Remove trailing backslash
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
cd /d "%SCRIPT_DIR%"

set "VENV_DIR=%SCRIPT_DIR%\.venv"

rem 1. Set up venv + install deps (cached after first run)
if not exist "%VENV_DIR%\" (
    echo ^> Creating Python venv at %VENV_DIR%
    python -m venv "%VENV_DIR%"
)
call "%VENV_DIR%\Scripts\activate.bat"
echo ^> Installing requirements (this may take a minute the first time) ...
pip install --quiet --upgrade pip
pip install --quiet -r "%SCRIPT_DIR%\requirements.txt"

rem 2. Build the C++ feature extractor.
echo ^> Building C++ feature extractor ...
if defined RACK_DIR (
    make -C "%SCRIPT_DIR%" RACK_DIR="%RACK_DIR%"
) else (
    make -C "%SCRIPT_DIR%"
)

rem 3. Parse --csv and --n-per-class arguments.
set "CSV_FILE="
set "N_PER_CLASS=80"
set "EXTRA_ARGS="

:parse_args
if "%~1"=="" goto done_parse
set "ARG=%~1"

if /i "%ARG%"=="--csv" (
    shift
    set "CSV_FILE=%~1"
    shift
    goto parse_args
)
set "PREFIX=%ARG:~0,6%"
if /i "%PREFIX%"=="--csv=" (
    set "CSV_FILE=%ARG:~6%"
    shift
    goto parse_args
)
if /i "%ARG%"=="--n-per-class" (
    shift
    set "N_PER_CLASS=%~1"
    shift
    goto parse_args
)
set "PREFIX=%ARG:~0,14%"
if /i "%PREFIX%"=="--n-per-class=" (
    set "N_PER_CLASS=%ARG:~14%"
    shift
    goto parse_args
)
set "EXTRA_ARGS=%EXTRA_ARGS% %ARG%"
shift
goto parse_args
:done_parse

if "%CSV_FILE%"=="" (
    echo ^> Generating synthetic dataset (n_per_class=%N_PER_CLASS%) ...
    if not exist "build\" mkdir build
    python generate_synthetic_dataset.py --out build --n-per-class %N_PER_CLASS%
    set "CSV_FILE=build\synthetic_dataset.csv"
)

rem 4. Train + emit C++ header fragment.
echo ^> Training model and emitting C++ header ...
python train_model.py --csv "%CSV_FILE%"%EXTRA_ARGS%

echo.
echo -- Done ---------------------------------------------------------------
echo Generated header fragment is at:
echo     %SCRIPT_DIR%\build\SirenTagClassifier.generated.cpp
echo.
echo Next steps:
echo   1. Open the generated file above.
echo   2. Copy its contents into the marked region of
echo          src\modules\Siren\SirenTagClassifier.cpp
echo   3. Bump SIREN_TAG_MODEL_VERSION in feature_config.py if the
echo      shape (number of features / classes) changed.
echo   4. Rebuild the plugin:  cd .. ^&^& make
echo.

endlocal
