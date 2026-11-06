# Invoke-Ocr.ps1
# Zero-dependency OCR using the Windows built-in OCR engine (Windows.Media.Ocr WinRT).
# This mirrors what PixPin does with local models (PixOCR2 + ONNX Runtime), but with the
# OCR engine the OS already ships. Output: one JSON object on stdout.

param(
  [string]$ImagePath = '',
  [string]$Lang = 'zh-Hans-CN'
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
try { Add-Type -AssemblyName System.Runtime.WindowsRuntime } catch { }

function Fail($msg) { ( @{ ok = $false; text = ''; message = $msg } | ConvertTo-Json -Compress ) }

if (-not $ImagePath -or -not (Test-Path -LiteralPath $ImagePath)) { Fail 'image not found'; exit 0 }
$ImagePath = [System.IO.Path]::GetFullPath($ImagePath)

try {
  $asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
    Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]

  function Await($winRtTask, $resultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($resultType)
    $netTask = $asTask.Invoke($null, @($winRtTask))
    try {
      $netTask.GetAwaiter().GetResult()
    } catch {
      throw $_.Exception.GetBaseException().Message
    }
  }

  [void][Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
  [void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics.Imaging, ContentType=WindowsRuntime]
  [void][Windows.Graphics.Imaging.SoftwareBitmap, Windows.Graphics.Imaging, ContentType=WindowsRuntime]
  [void][Windows.Media.Ocr.OcrEngine, Windows.Foundation.UniversalApiContract, ContentType=WindowsRuntime]
  [void][Windows.Globalization.Language, Windows.Globalization, ContentType=WindowsRuntime]

  $file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($ImagePath)) ([Windows.Storage.StorageFile])
  $stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
  $decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
  $bitmap = Await ($decoder.GetSoftwareBitmapAsync([Windows.Graphics.Imaging.BitmapPixelFormat]::Bgra8, [Windows.Graphics.Imaging.BitmapAlphaMode]::Premultiplied)) ([Windows.Graphics.Imaging.SoftwareBitmap])

  $engine = $null
  try { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage([Windows.Globalization.Language]::new($Lang)) } catch { }
  if (-not $engine) { try { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() } catch { } }
  if (-not $engine) { Fail 'no OCR language pack installed (Windows OCR)'; exit 0 }

  $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
  $text = ''
  $positions = @()
  $lineIndex = 0
  foreach ($line in $result.Lines) {
    if ($lineIndex -gt 0) { $text += "`n" }
    $lineStart = $text.Length
    $lineText = $line.Text
    $text += $lineText
    $searchFrom = 0
    foreach ($w in $line.Words) {
      $start = $lineText.IndexOf($w.Text, $searchFrom, [System.StringComparison]::Ordinal)
      if ($start -lt 0) { throw 'OCR word is absent from the recognized line' }
      $positions += @{
        start = $lineStart + $start
        length = $w.Text.Length
        x = $w.BoundingRect.X
        y = $w.BoundingRect.Y
        width = $w.BoundingRect.Width
        height = $w.BoundingRect.Height
      }
      $searchFrom = $start + $w.Text.Length
    }
    $lineIndex++
  }
  $stream.Dispose()
  $bitmap.Dispose()
  ( @{ ok = $true; text = $text; words = @($positions); message = "Windows OCR ($($engine.RecognizerLanguage.LanguageTag))" } | ConvertTo-Json -Depth 5 -Compress )
} catch {
  Fail ("Windows OCR failed: " + $_.Exception.Message)
  exit 0
}
