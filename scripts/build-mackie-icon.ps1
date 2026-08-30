param(
    [string]$Source = (Join-Path $PSScriptRoot '../src/FaderBridge.MackieHost/assets/mackie-icon-source.png'),
    [string]$Output = (Join-Path $PSScriptRoot '../src/FaderBridge.MackieHost/assets/WindowsFaderBridge.Mackie.ico')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Package the project-owned artwork in standard Windows icon resolutions.
$sourceImage = [Drawing.Image]::FromFile((Resolve-Path -LiteralPath $Source))
$streams = [Collections.Generic.List[IO.MemoryStream]]::new()
try {
    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    foreach ($size in $sizes) {
        $bitmap = [Drawing.Bitmap]::new($size, $size)
        $graphics = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.DrawImage($sourceImage, 0, 0, $size, $size)
            $stream = [IO.MemoryStream]::new()
            $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $streams.Add($stream)
        } finally { $graphics.Dispose(); $bitmap.Dispose() }
    }
    $file = [IO.File]::Create([IO.Path]::GetFullPath($Output))
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $encodedSize = if ($sizes[$i] -eq 256) { 0 } else { $sizes[$i] }
            $writer.Write([byte]$encodedSize); $writer.Write([byte]$encodedSize)
            $writer.Write([byte]0); $writer.Write([byte]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$streams[$i].Length); $writer.Write([uint32]$offset)
            $offset += $streams[$i].Length
        }
        foreach ($stream in $streams) { $writer.Write($stream.ToArray()) }
    } finally { $writer.Dispose(); $file.Dispose() }
} finally { foreach ($stream in $streams) { $stream.Dispose() }; $sourceImage.Dispose() }
Write-Output "Packaged Mackie icon: $Output"
