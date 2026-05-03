param(
    [string[]]$LogPath = @(
        "C:\SteamLibrary\steamapps\common\Windrose\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns\WindroseTextSigns.log",
        "C:\Games\WindowsServer\R5\Binaries\Win64\ue4ss\Mods\WindroseTextSigns\WindroseTextSigns.log"
    ),
    [string]$OutJson = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function New-ResultObject {
    return [ordered]@{
        sourceLogs = @()
        probeLines = 0
        woodenItemsFound = $null
        woodenItems = @{}
        widgetCandidates = @()
        functionCandidates = @()
    }
}

function Get-OrAddItem([hashtable]$table, [int]$index) {
    $key = [string]$index
    if (-not $table.ContainsKey($key)) {
        $table[$key] = [ordered]@{
            index = $index
            object = ""
            class = ""
            outer = ""
            fields = @{}
            fieldCountReported = $null
        }
    }
    return $table[$key]
}

$result = New-ResultObject

foreach ($path in $LogPath) {
    if (-not (Test-Path -LiteralPath $path)) {
        continue
    }
    $result.sourceLogs += $path
    $lines = Get-Content -LiteralPath $path
    foreach ($line in $lines) {
        if ($line -notmatch "\[buildmenu-probe\]") {
            continue
        }

        $result.probeLines++

        if ($line -match "wooden_items_found=(\d+)") {
            $result.woodenItemsFound = [int]$Matches[1]
            continue
        }
        if ($line -match "wooden_item index=(\d+)\s+object=(.*?)\s+class=(.*?)\s+outer=(.*)$") {
            $item = Get-OrAddItem -table $result.woodenItems -index ([int]$Matches[1])
            $item.object = $Matches[2].Trim()
            $item.class = $Matches[3].Trim()
            $item.outer = $Matches[4].Trim()
            continue
        }
        if ($line -match "wooden_item_field index=(\d+)\s+prop=([^\s]+)\s+value=(.*)$") {
            $item = Get-OrAddItem -table $result.woodenItems -index ([int]$Matches[1])
            $prop = $Matches[2].Trim()
            $value = $Matches[3].Trim()
            $item.fields[$prop] = $value
            continue
        }
        if ($line -match "wooden_item_field_count index=(\d+)\s+count=(\d+)") {
            $item = Get-OrAddItem -table $result.woodenItems -index ([int]$Matches[1])
            $item.fieldCountReported = [int]$Matches[2]
            continue
        }
        if ($line -match "widget_candidate index=\d+\s+value=(.*)$") {
            $result.widgetCandidates += $Matches[1].Trim()
            continue
        }
        if ($line -match "function_candidate index=\d+\s+value=(.*)$") {
            $result.functionCandidates += $Matches[1].Trim()
            continue
        }
    }
}

if ($result.probeLines -eq 0) {
    Write-Host "No [buildmenu-probe] lines found."
    Write-Host "Run the in-game probe first (F9 or Config\\run_buildmenu_probe.flag), then re-run this script."
    exit 0
}

$orderedItems = @($result.woodenItems.Values | Sort-Object index)
$iconSignals = @()
$recipeSignals = @()
$classSignals = @()

foreach ($item in $orderedItems) {
    foreach ($prop in $item.fields.Keys) {
        $p = $prop.ToLowerInvariant()
        $value = $item.fields[$prop]
        if ($p -match "icon|sprite|texture") {
            $iconSignals += [ordered]@{ index = $item.index; prop = $prop; value = $value }
        }
        if ($p -match "recipe|construct|build") {
            $recipeSignals += [ordered]@{ index = $item.index; prop = $prop; value = $value }
        }
        if ($p -match "class|actor|blueprint") {
            $classSignals += [ordered]@{ index = $item.index; prop = $prop; value = $value }
        }
    }
}

Write-Host ("Source logs: " + ($result.sourceLogs -join "; "))
Write-Host ("Probe lines: " + $result.probeLines)
Write-Host ("Wooden items found (reported): " + $(if ($null -eq $result.woodenItemsFound) { "unknown" } else { $result.woodenItemsFound }))
Write-Host ("Wooden items parsed: " + $orderedItems.Count)
Write-Host ("Widget candidates: " + $result.widgetCandidates.Count)
Write-Host ("Function candidates: " + $result.functionCandidates.Count)
Write-Host ""

foreach ($item in $orderedItems) {
    Write-Host ("[item " + $item.index + "] " + $item.object)
    Write-Host ("  class=" + $item.class)
    Write-Host ("  outer=" + $item.outer)
    Write-Host ("  fields=" + $item.fields.Count + " reported=" + $(if ($null -eq $item.fieldCountReported) { "n/a" } else { $item.fieldCountReported }))
}

Write-Host ""
Write-Host ("Icon-related fields: " + $iconSignals.Count)
foreach ($row in $iconSignals) {
    Write-Host ("  item=" + $row.index + " prop=" + $row.prop + " value=" + $row.value)
}

Write-Host ""
Write-Host ("Recipe/build-related fields: " + $recipeSignals.Count)
foreach ($row in $recipeSignals) {
    Write-Host ("  item=" + $row.index + " prop=" + $row.prop + " value=" + $row.value)
}

Write-Host ""
Write-Host ("Class/actor-related fields: " + $classSignals.Count)
foreach ($row in $classSignals) {
    Write-Host ("  item=" + $row.index + " prop=" + $row.prop + " value=" + $row.value)
}

if ($OutJson -ne "") {
    $jsonPayload = [ordered]@{
        sourceLogs = $result.sourceLogs
        probeLines = $result.probeLines
        woodenItemsFound = $result.woodenItemsFound
        woodenItems = $orderedItems
        iconSignals = $iconSignals
        recipeSignals = $recipeSignals
        classSignals = $classSignals
        widgetCandidates = ($result.widgetCandidates | Select-Object -Unique)
        functionCandidates = ($result.functionCandidates | Select-Object -Unique)
    }
    $jsonText = $jsonPayload | ConvertTo-Json -Depth 8
    Set-Content -LiteralPath $OutJson -Value $jsonText -Encoding UTF8
    Write-Host ""
    Write-Host ("Wrote JSON summary: " + $OutJson)
}
