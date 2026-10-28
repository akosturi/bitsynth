@echo off
setlocal
rem Set this to 8 or 9 to select the signed output bit depth.
set "RAW_TO_H_OUTPUT_BITS=8"
set "RAW_TO_H_SELF=%~f0"
if "%~1"=="" goto choose
:next
set "RAW_TO_H_INPUT=%~1"
call :convert
shift
if not "%~1"=="" goto next
goto done
:choose
set "RAW_TO_H_INPUT="
call :convert
:done
pause
exit /b
:convert
powershell.exe -NoProfile -STA -Command "$s = [IO.File]::ReadAllText($env:RAW_TO_H_SELF); $p = $s -split ('(?m)^# POWERSHELL_' + 'START\r?$'), 2; & ([scriptblock]::Create($p[1]))"
exit /b
# POWERSHELL_START
$ErrorActionPreference = 'Stop'
try {
    $path = $env:RAW_TO_H_INPUT
    if ([string]::IsNullOrWhiteSpace($path)) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'RAW audio (*.raw)|*.raw'
        $dialog.Title = 'Select a mono signed 16-bit PCM RAW file'
        if ($dialog.ShowDialog() -ne 'OK') { return }
        $path = $dialog.FileName
        $dialog.Dispose()
    }
    $path = [IO.Path]::GetFullPath($path)
    if ([IO.Path]::GetExtension($path) -ine '.raw') { throw 'Please select a .raw file.' }
    [byte[]]$data = [IO.File]::ReadAllBytes($path)
    if ($data.Length -eq 0) { throw 'The RAW file is empty.' }
    if (($data.Length % 2) -ne 0) {
        throw 'The PCM16 RAW file has an odd byte count.'
    }

    $outputBits = 0
    if (-not [int]::TryParse($env:RAW_TO_H_OUTPUT_BITS, [ref]$outputBits) -or
        ($outputBits -ne 8 -and $outputBits -ne 9)) {
        throw 'RAW_TO_H_OUTPUT_BITS must be 8 or 9.'
    }
    $shift = 16 - $outputBits
    $sampleCount = $data.Length / 2
    $samples = New-Object 'System.Collections.Generic.List[int]'
    for ($i = 0; $i -lt $data.Length; $i += 2) {
        # RAW input is little-endian signed PCM16. Arithmetic shifting keeps
        # the sign and reduces it to the requested signed bit depth.
        $sample16 = [BitConverter]::ToInt16($data, $i)
        $samples.Add(([int]$sample16) -shr $shift)
    }

    $output = [IO.Path]::ChangeExtension($path, '.h')
    if ([IO.File]::Exists($output)) {
        $answer = Read-Host ('Already exists: ' + $output + ' - overwrite? [y/N]')
        if ($answer -ine 'y') { Write-Host 'Skipped.'; return }
    }

    $base = [IO.Path]::GetFileNameWithoutExtension($path)
    $identifier = [regex]::Replace($base, '[^A-Za-z0-9_]', '_')
    if ($identifier -notmatch '^[A-Za-z]') { $identifier = 'sample_' + $identifier }
    $name = $identifier + 'Sample'

    $builder = New-Object Text.StringBuilder
    [void]$builder.AppendLine('#pragma once')
    [void]$builder.AppendLine('#include <stdint.h>')
    [void]$builder.AppendLine('#include <avr/pgmspace.h>')
    [void]$builder.AppendLine('')
    [void]$builder.AppendLine(('// Converted from mono little-endian signed PCM16 to signed {0}-bit PCM.' -f $outputBits))
    [void]$builder.AppendLine('// RAW has no sample-rate metadata. Set SampleRate below if needed.')
    $type = if ($outputBits -eq 8) { 'int8_t' } else { 'int16_t' }
    [void]$builder.AppendLine(('const {0} {1}[] PROGMEM = {{' -f $type, $name))
    for ($i = 0; $i -lt $samples.Count; $i += 16) {
        $last = [Math]::Min($i + 15, $samples.Count - 1)
        $line = '    ' + ($samples.GetRange($i, $last - $i + 1) -join ', ')
        if ($last -lt $samples.Count - 1) { $line += ',' }
        [void]$builder.AppendLine($line)
    }
    [void]$builder.AppendLine('};')
    [void]$builder.AppendLine(('const uint32_t {0}Length = sizeof({0}) / sizeof({0}[0]);' -f $name))
    [void]$builder.AppendLine(('const uint32_t {0}Rate = 20000;' -f $name))
    [void]$builder.AppendLine(('const uint8_t {0}Bits = {1};' -f $name, $outputBits))
    $encoding = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($output, $builder.ToString(), $encoding)
    Write-Host ('Created: ' + $output)
    Write-Host ('{0} samples | signed {1}-bit | array: {2} | default rate: 20000 Hz' -f $sampleCount, $outputBits, $name)
}
catch {
    Write-Host ('Error: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
