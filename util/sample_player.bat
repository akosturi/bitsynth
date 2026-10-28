@echo off
setlocal
set "SAMPLE_PLAYER_SELF=%~f0"
set "SAMPLE_PLAYER_HEADER=%~1"
powershell.exe -NoProfile -STA -Command "$s = [IO.File]::ReadAllText($env:SAMPLE_PLAYER_SELF); $p = $s -split ('(?m)^# POWERSHELL_' + 'START\r?$'), 2; & ([scriptblock]::Create($p[1]))"
pause
exit /b
# POWERSHELL_START
$ErrorActionPreference = 'Stop'
try {
    $path = $env:SAMPLE_PLAYER_HEADER
    if ([string]::IsNullOrWhiteSpace($path)) {
        Add-Type -AssemblyName System.Windows.Forms
        $dialog = New-Object System.Windows.Forms.OpenFileDialog
        $dialog.Filter = 'Sample headers (*.h)|*.h'
        $dialog.Title = 'Select a signed PCM sample header'
        if ($dialog.ShowDialog() -ne 'OK') { return }
        $path = $dialog.FileName
        $dialog.Dispose()
    }

    $text = [IO.File]::ReadAllText($path)
    $text = [regex]::Replace($text, '(?s)/\*.*?\*/|//[^\r\n]*', '')
    $match = [regex]::Match($text, '\b(int8_t|int16_t|signed\s+char|signed\s+short|short)\s+(\w+)\s*\[\s*\d*\s*\]\s*(?:PROGMEM\s*)?=\s*\{([^}]*)\}', 'Singleline, IgnoreCase')
    if (-not $match.Success) { throw 'No signed 8/9-bit sample array found.' }
    $type = [regex]::Replace($match.Groups[1].Value.ToLowerInvariant(), '\s+', ' ')
    $name = $match.Groups[2].Value
    $values = $match.Groups[3].Value.Split(',')
    $storageBits = if ($type -eq 'int8_t' -or $type -eq 'signed char') { 8 } else { 16 }
    $samples = New-Object 'System.Collections.Generic.List[int]'
    foreach ($value in $values) {
        $v = $value.Trim()
        if ($v -eq '') { continue }
        if ($v -match '^0[xX][0-9a-fA-F]+$') {
            $n = [Convert]::ToInt32($v.Substring(2), 16)
            $storageLimit = 1 -shl $storageBits
            if ($n -ge $storageLimit) { throw "Sample value outside the declared $storageBits-bit type: $v" }
            if ($n -ge (1 -shl ($storageBits - 1))) { $n -= $storageLimit }
        }
        elseif ($v -match '^[+-]?\d+$') { $n = [int]$v }
        else { throw "Unsupported sample value: $v" }
        $storageMin = -(1 -shl ($storageBits - 1))
        $storageMax = (1 -shl ($storageBits - 1)) - 1
        if ($n -lt $storageMin -or $n -gt $storageMax) {
            throw "Sample value outside the declared $storageBits-bit signed type: $v"
        }
        $samples.Add($n)
    }
    if ($samples.Count -eq 0) { throw 'The sample is empty.' }

    $bits = 0
    $bitsMatch = [regex]::Match($text, '\b' + [regex]::Escape($name + 'Bits') + '\s*=\s*(\d+)\s*;')
    if ($bitsMatch.Success) {
        $bits = [int]$bitsMatch.Groups[1].Value
    } elseif ($storageBits -eq 8) {
        $bits = 8
    } else {
        $withinNineBits = $true
        foreach ($sample in $samples) {
            if ($sample -lt -256 -or $sample -gt 255) { $withinNineBits = $false; break }
        }
        $bits = if ($withinNineBits) { 9 } else { 16 }
        Write-Host ("No bit-depth metadata found; inferred signed $bits-bit PCM.")
    }
    if ($bits -ne 8 -and $bits -ne 9 -and $bits -ne 16) {
        throw 'Unsupported signed PCM bit depth; expected 8, 9, or 16.'
    }
    if ($bits -gt $storageBits) { throw 'Bit depth does not fit the declared array type.' }
    $sampleMin = -(1 -shl ($bits - 1))
    $sampleMax = (1 -shl ($bits - 1)) - 1
    foreach ($sample in $samples) {
        if ($sample -lt $sampleMin -or $sample -gt $sampleMax) {
            throw "Sample value $sample is outside signed $bits-bit range."
        }
    }

    $rate = 20000
    $rateMatch = [regex]::Match($text, '\b' + [regex]::Escape($name + 'Rate') + '\s*=\s*(\d+)\s*;')
    if ($rateMatch.Success) { $rate = [int]$rateMatch.Groups[1].Value }
    else { Write-Host 'No sample rate found; assuming 20000 Hz.' }
    if ($rate -lt 1000 -or $rate -gt 192000) { throw 'Invalid sample rate.' }

    # Windows WAV playback uses signed PCM16 here. Scaling preserves the
    # original signed sample level for both 8-bit and 9-bit headers.
    $scaleShift = 16 - $bits
    $pcm16 = New-Object 'System.Collections.Generic.List[int16]'
    foreach ($sample in $samples) {
        $pcm16.Add([int16]($sample -shl $scaleShift))
    }
    $dataLength = $pcm16.Count * 2

    $stream = New-Object IO.MemoryStream
    $writer = New-Object IO.BinaryWriter($stream)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([uint32](36 + $dataLength))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVEfmt '))
    $writer.Write([uint32]16)
    $writer.Write([uint16]1)
    $writer.Write([uint16]1)
    $writer.Write([uint32]$rate)
    $writer.Write([uint32]($rate * 2))
    $writer.Write([uint16]2)
    $writer.Write([uint16]16)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([uint32]$dataLength)
    foreach ($sample in $pcm16) { $writer.Write($sample) }
    $writer.Flush()
    $stream.Position = 0
    Write-Host ([IO.Path]::GetFileName($path))
    Write-Host ('{0} samples | signed {1}-bit | {2} Hz | {3:N0} ms' -f $samples.Count, $bits, $rate, (1000.0 * $samples.Count / $rate))
    $player = New-Object System.Media.SoundPlayer
    $player.Stream = $stream
    $player.PlaySync()
    $player.Dispose()
    $writer.Dispose()
    $stream.Dispose()
}
catch {
    Write-Host ('Error: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
