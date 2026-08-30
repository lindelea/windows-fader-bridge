param(
    [Parameter(Mandatory)][string]$InputPreset,
    [ValidateRange(1,3)][int]$DawSlot = 2,
    [string]$OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\mackie-presets'),
    [string]$CommandTable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\mackie-touchscreen-example.csv')
)
$ErrorActionPreference = 'Stop'
# Local transformation only. The user supplies an iMAP export; no factory map,
# vendor code, private SysEx, MIDI I/O or live application settings are shipped.
# Layout/serialization verified against the iMAP 1.25.3 editor, export version 1.32.
$inputPath = (Resolve-Path -LiteralPath $InputPreset).Path
if ((Get-Item -LiteralPath $inputPath).Length -gt 8MB) { throw 'Preset exceeds the supported size.' }
$readerSettings = [System.Xml.XmlReaderSettings]::new()
$readerSettings.DtdProcessing = [System.Xml.DtdProcessing]::Prohibit
$readerSettings.XmlResolver = $null
$readerSettings.MaxCharactersInDocument = 8MB
$reader = [System.Xml.XmlReader]::Create($inputPath, $readerSettings)
$preset = [System.Xml.XmlDocument]::new()
$preset.XmlResolver = $null
try { $preset.Load($reader) } finally { $reader.Dispose() }
if ($preset.DocumentElement.Name -cne 'slots' -or $preset.DocumentElement.GetAttribute('ver') -cne '1.32') {
    throw 'Expected a full .imap export (slots, version 1.32); other formats need editor verification.'
}
$slots = @($preset.SelectNodes('/slots/slot'))
if ($slots.Count -ne 3 -or @($slots | ForEach-Object { $_.GetAttribute('id') } | Sort-Object -Unique).Count -ne 3) {
    throw 'Expected three distinct DAW slots.'
}
$slot = $preset.SelectSingleNode("/slots/slot[@id='$($DawSlot-1)']")
if ($null -eq $slot -or $slot.GetAttribute('current') -cne '2') { throw 'Selected slot must already use Cubase; no DAW mode is changed.' }
$modes = @($slot.SelectNodes("mode[@id='2' and @mode='cubase']"))
if ($modes.Count -ne 1) { throw 'Expected exactly one Cubase mapping.' }
$mode = $modes[0]
$controls = @($mode.SelectNodes('ctl'))
if ($controls.Count -ne 226 -or @($controls | ForEach-Object { $_.GetAttribute('id') } | Sort-Object -Unique).Count -ne 226) {
    throw 'Unrecognized Cubase control layout; refusing to guess positions.'
}
$rows = @(Import-Csv -LiteralPath $CommandTable -Encoding UTF8)
if ($rows.Count -ne 80) { throw 'Expected exactly 80 touchscreen commands.' }
$catalog = Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'src\FaderBridge.MackieHost\CommandCatalog.h') -Raw
$pages = @('System','Window','Files','Media','Edit')
$originalOtherSlots = @($slots | Where-Object { $_ -ne $slot } | ForEach-Object { $_.OuterXml })
$originalOtherModes = @($slot.SelectNodes('mode') | Where-Object { $_ -ne $mode } | ForEach-Object { $_.OuterXml })
$originalCore = @($controls | Where-Object { [int]$_.GetAttribute('id') -lt 101 -or [int]$_.GetAttribute('id') -gt 180 } | ForEach-Object { $_.OuterXml })
for ($i=0; $i -lt 80; ++$i) {
    $row = $rows[$i]
    if ([int]$row.MidiChannel -ne 16 -or [int]$row.Note -ne $i -or [int]$row.Button -ne ($i % 16 + 1) -or
        $row.Page -cne $pages[[int][Math]::Floor($i / 16)] -or $row.CommandId -cnotmatch '^[A-Za-z][A-Za-z0-9]+$' -or
        !$catalog.Contains('L"' + $row.CommandId + '"') -or $row.ScreenLabel -cnotmatch '^[A-Za-z0-9 ]{1,12}$') {
        throw "Invalid command table row $i."
    }
    $control = $mode.SelectSingleNode("ctl[@id='$($i+101)']")
    if ($null -eq $control -or $control.GetAttribute('kind') -cne 'button') { throw "Unexpected touchscreen control $i." }
    # These properties correspond to the official editor's Note / Channel /
    # Message Value / Name. Unrelated attributes and all other controls survive.
    $control.SetAttribute('valueType','midi')
    $control.SetAttribute('type','note')
    $control.SetAttribute('chan','16')
    $control.SetAttribute('value', [string]$i)
    $control.SetAttribute('name', $row.ScreenLabel)
    # Remove former keyboard actions so a button cannot perform two actions.
    foreach ($field in @('ctrl','shift','alt','cmd','key','rotate')) { $control.SetAttribute($field,'0') }
    $control.SetAttribute('keyName','')
    $control.SetAttribute('val1','-1'); $control.SetAttribute('val2','-1')
}
$afterOtherSlots = @($slots | Where-Object { $_ -ne $slot } | ForEach-Object { $_.OuterXml })
$afterOtherModes = @($slot.SelectNodes('mode') | Where-Object { $_ -ne $mode } | ForEach-Object { $_.OuterXml })
$afterCore = @($controls | Where-Object { [int]$_.GetAttribute('id') -lt 101 -or [int]$_.GetAttribute('id') -gt 180 } | ForEach-Object { $_.OuterXml })
if (($originalOtherSlots -join "`n") -cne ($afterOtherSlots -join "`n") -or
    ($originalOtherModes -join "`n") -cne ($afterOtherModes -join "`n") -or
    ($originalCore -join "`n") -cne ($afterCore -join "`n")) { throw 'Non-touchscreen data changed.' }
$outputRoot = [System.IO.Path]::GetFullPath($OutputDirectory)
$fullPath = Join-Path $outputRoot "Windows80-DAW$DawSlot.imap"
$singlePath = Join-Path $outputRoot "Windows80-DAW$DawSlot.p1n-daw"
foreach ($path in @($fullPath,$singlePath)) {
    if ($path -ieq $inputPath -or (Test-Path -LiteralPath $path)) { throw "Refusing to overwrite: $path" }
}
[System.IO.Directory]::CreateDirectory($outputRoot) | Out-Null
$writerSettings = [System.Xml.XmlWriterSettings]::new()
$writerSettings.Encoding = [System.Text.UTF8Encoding]::new($false)
$writerSettings.Indent = $true
function Save-PresetXml($document, $path) {
    $writer = [System.Xml.XmlWriter]::Create($path, $writerSettings)
    try { $document.Save($writer) } finally { $writer.Dispose() }
}
Save-PresetXml $preset $fullPath
$single = [System.Xml.XmlDocument]::new()
$single.AppendChild($single.ImportNode($mode,$true)) | Out-Null
Save-PresetXml $single $singlePath
Write-Host "Created $fullPath"
Write-Host "Created $singlePath"
Write-Host 'Only 80 touchscreen controls changed. Other slots, DAW modes, faders, encoders and hardware buttons are preserved.'
Write-Host 'Nothing was loaded into iMAP or sent to a MIDI device. Verify through the official editor before use.'
