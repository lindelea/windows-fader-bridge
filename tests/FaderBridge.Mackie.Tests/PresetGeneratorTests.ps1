$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$generator = Join-Path $root 'scripts\make-mackie-touchscreen-preset.ps1'
$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('wfb-preset-tests-' + [guid]::NewGuid().ToString('N'))
[System.IO.Directory]::CreateDirectory($testRoot) | Out-Null
$script:checks = 0
function Assert-Preset($condition, $message) {
    ++$script:checks
    if (!$condition) { throw $message }
}
# All fixture content is project-created; no vendor presets are in this test.
$fixture = [System.Xml.XmlDocument]::new()
$slots = $fixture.CreateElement('slots'); $slots.SetAttribute('current','2'); $slots.SetAttribute('ver','1.32')
$fixture.AppendChild($slots) | Out-Null
for ($s=0; $s -lt 3; ++$s) {
    $slot=$fixture.CreateElement('slot'); $slot.SetAttribute('id',[string]$s); $slot.SetAttribute('current','2')
    $slots.AppendChild($slot) | Out-Null
    $mode=$fixture.CreateElement('mode'); $mode.SetAttribute('id','2'); $mode.SetAttribute('mode','cubase')
    $slot.AppendChild($mode) | Out-Null
    for ($i=1; $i -le 226; ++$i) {
        $ctl=$fixture.CreateElement('ctl'); $ctl.SetAttribute('id',[string]$i)
        $ctl.SetAttribute('kind','button'); $ctl.SetAttribute('name',"original-$s-$i")
        $ctl.SetAttribute('valueType','hotkey'); $ctl.SetAttribute('ctrl','1')
        $ctl.SetAttribute('key','65'); $ctl.SetAttribute('keyName','A'); $ctl.SetAttribute('custom','preserve')
        $mode.AppendChild($ctl) | Out-Null
    }
    $other=$fixture.CreateElement('mode'); $other.SetAttribute('id','99'); $other.SetAttribute('mode','test-owned-other-mode')
    $slot.AppendChild($other) | Out-Null
}
$inputPath=Join-Path $testRoot 'input.imap'; $fixture.Save($inputPath)
$inputHash=(Get-FileHash -LiteralPath $inputPath).Hash
$out=Join-Path $testRoot 'valid'
& $generator -InputPreset $inputPath -DawSlot 2 -OutputDirectory $out
$full=[xml](Get-Content -LiteralPath (Join-Path $out 'Windows80-DAW2.imap') -Raw)
$single=[xml](Get-Content -LiteralPath (Join-Path $out 'Windows80-DAW2.p1n-daw') -Raw)
$table=@(Import-Csv -LiteralPath (Join-Path $root 'docs\mackie-touchscreen-example.csv'))
Assert-Preset ((Get-FileHash -LiteralPath $inputPath).Hash -ceq $inputHash) 'Generator overwrote input.'
foreach ($s in @(0,2)) {
    Assert-Preset ($full.slots.slot[$s].OuterXml -ceq $fixture.slots.slot[$s].OuterXml) 'Another slot changed.'
}
$target=$full.slots.slot[1].mode[0]
Assert-Preset ($target.OuterXml -ceq $single.DocumentElement.OuterXml) 'Full and single exports differ.'
Assert-Preset ($full.slots.slot[1].mode[1].OuterXml -ceq $fixture.slots.slot[1].mode[1].OuterXml) 'Other DAW mode changed.'
for ($i=1; $i -le 226; ++$i) {
    $ctl=$target.SelectSingleNode("ctl[@id='$i']")
    if ($i -ge 101 -and $i -le 180) {
        Assert-Preset ($ctl.GetAttribute('type') -ceq 'note' -and $ctl.GetAttribute('chan') -ceq '16' -and
            $ctl.GetAttribute('value') -ceq [string]($i-101) -and $ctl.GetAttribute('valueType') -ceq 'midi' -and
            $ctl.GetAttribute('name') -ceq $table[$i-101].ScreenLabel) 'Touchscreen mapping mismatch.'
        Assert-Preset ($ctl.GetAttribute('key') -ceq '0' -and $ctl.GetAttribute('ctrl') -ceq '0' -and
            $ctl.GetAttribute('keyName') -ceq '' -and $ctl.GetAttribute('custom') -ceq 'preserve') 'Hotkey remained or unknown metadata changed.'
    } else {
        Assert-Preset ($ctl.OuterXml -ceq $fixture.slots.slot[1].mode[0].SelectSingleNode("ctl[@id='$i']").OuterXml) 'Core control changed.'
    }
}
function Assert-Rejected($path, $output, $tablePath='') {
    $rejected=$false
    try {
        if ($tablePath) { & $generator -InputPreset $path -DawSlot 2 -OutputDirectory $output -CommandTable $tablePath }
        else { & $generator -InputPreset $path -DawSlot 2 -OutputDirectory $output }
    } catch { $rejected=$true }
    Assert-Preset $rejected 'Invalid input was accepted.'
}
$fullPath=Join-Path $out 'Windows80-DAW2.imap'; $outputHash=(Get-FileHash -LiteralPath $fullPath).Hash
Assert-Rejected $inputPath $out
Assert-Preset ((Get-FileHash -LiteralPath $fullPath).Hash -ceq $outputHash) 'Existing output was overwritten.'
Assert-Rejected (Join-Path $out 'Windows80-DAW2.p1n-daw') (Join-Path $testRoot 'reject-single')
$bad=$fixture.CloneNode($true); $bad.DocumentElement.SetAttribute('ver','future')
$badPath=Join-Path $testRoot 'bad-version.imap'; $bad.Save($badPath)
Assert-Rejected $badPath (Join-Path $testRoot 'reject-version')
$bad=$fixture.CloneNode($true); $bad.slots.slot[1].mode[0].ctl[0].SetAttribute('id','101')
$badPath=Join-Path $testRoot 'duplicate-control.imap'; $bad.Save($badPath)
Assert-Rejected $badPath (Join-Path $testRoot 'reject-duplicate')
$bad=$fixture.CloneNode($true); $bad.slots.slot[1].SetAttribute('current','3')
$badPath=Join-Path $testRoot 'wrong-daw.imap'; $bad.Save($badPath)
Assert-Rejected $badPath (Join-Path $testRoot 'reject-daw')
$bad=$fixture.CloneNode($true); $bad.slots.slot[1].mode[0].ctl[100].SetAttribute('kind','fader')
$badPath=Join-Path $testRoot 'wrong-control.imap'; $bad.Save($badPath)
Assert-Rejected $badPath (Join-Path $testRoot 'reject-control')
$badTable=@(Import-Csv -LiteralPath (Join-Path $root 'docs\mackie-touchscreen-example.csv'))
$badTable[79].CommandId='UnlistedCommand'; $csvPath=Join-Path $testRoot 'invalid.csv'
$badTable | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
Assert-Rejected $inputPath (Join-Path $testRoot 'reject-command') $csvPath
Write-Host "PASS: $script:checks preset-generator checks; no vendor fixture, no MIDI ports opened, no live settings changed."
Write-Host "Synthetic test artifacts: $testRoot"
