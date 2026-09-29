# A/B cost of the DSP2 round-trip hold (MD_PAIR_HOLD_DSP2_US) on the
# Monomachine in pair mode, at the default DSP lead.
#
# Runs mdParallelTransportBenchmark unpaced (realtime = wall time per second
# of audio, lower is better) alternating without and with the hold, so slow
# drift of the machine hits both arms alike. Prints each run and the medians.
# Decision rule (implementation log, 2026-09-29): enable the hold if it costs
# at most ~5 points.
#
# Run it on an idle machine: close the DAW, browsers, builds. The script
# refuses to start above -MaxIdleLoad percent CPU unless -Force is given.
#
# Usage: .\bench-hold-dsp2.ps1 [-Runs 5] [-Seconds 60] [-Warmup 15] [-HoldUs 30] [-Build]

param(
	[int]$Runs = 5,
	[int]$Seconds = 60,
	[int]$Warmup = 15,
	[double]$HoldUs = 30,
	[int]$MaxIdleLoad = 10,
	[switch]$Build,
	[switch]$Force
)

$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$buildDir = Join-Path $root 'temp\cmake_vs22'
$exe = Join-Path $buildDir 'source\elektron\md\mdJucePlugin\Release\mdParallelTransportBenchmark.exe'
$romDir = Join-Path $env:LOCALAPPDATA 'Programs\Gearmulator-Elektron'

if($Build)
{
	cmake --build $buildDir --config Release -j 6 --target mdParallelTransportBenchmark | Out-Null
	if($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }
}
if(-not (Test-Path $exe)) { throw "missing $exe (run with -Build)" }

# The bench finds the MM ROM next to the MD one (--model mm).
$env:GEARMULATOR_MD_FIRMWARE_BIN = Join-Path $romDir 'elektron_sps1-1uw_os1.63.bin'
$env:MDMM_TRANSPORT = 'pair'
# Default lead and placement: the configuration a user gets.
Remove-Item Env:MD_PAIR_LEAD_US, Env:MD_PAIR_UC_LEAD_US, Env:MDMM_PAIR_AFFINITY, Env:MD_PAIR_HOLD_DSP2_US -ErrorAction SilentlyContinue

$load = (Get-CimInstance Win32_Processor | Measure-Object LoadPercentage -Average).Average
Write-Host ("CPU load before start: {0:N0}%" -f $load)
if($load -gt $MaxIdleLoad -and -not $Force)
{
	throw "machine not idle ($load% > $MaxIdleLoad%). Close other work or pass -Force."
}

$timeoutMs = ($Warmup + $Seconds + 120) * 1000
$benchArgs = @('--model', 'mm', '--rate', '44100', '--mode', 'parallel', '--paced', '0',
	'--warmup', $Warmup, '--seconds', $Seconds)

function Invoke-Bench([bool]$hold)
{
	if($hold) { $env:MD_PAIR_HOLD_DSP2_US = "$HoldUs" }
	else { Remove-Item Env:MD_PAIR_HOLD_DSP2_US -ErrorAction SilentlyContinue }
	$out = [IO.Path]::GetTempFileName()
	$err = [IO.Path]::GetTempFileName()
	try
	{
		$p = Start-Process -FilePath $exe -ArgumentList $benchArgs -NoNewWindow -PassThru `
			-RedirectStandardOutput $out -RedirectStandardError $err
		if(-not $p.WaitForExit($timeoutMs))
		{
			$p.Kill()
			throw "bench timed out after $($timeoutMs / 1000) s"
		}
		$line = Select-String -Path $out -Pattern 'realtime=([\d.]+)%' | Select-Object -Last 1
		if(-not $line) { throw "no realtime line (exit $($p.ExitCode)): $(Get-Content $err -Raw)" }
		return [double]$line.Matches[0].Groups[1].Value
	}
	finally
	{
		Remove-Item $out, $err -ErrorAction SilentlyContinue
	}
}

function Get-Median([double[]]$values)
{
	$s = $values | Sort-Object
	$n = $s.Count
	if($n % 2) { return $s[($n - 1) / 2] }
	return ($s[$n / 2 - 1] + $s[$n / 2]) / 2
}

$off = @()
$on = @()
for($r = 1; $r -le $Runs; ++$r)
{
	# Alternate which arm goes first, so neither always runs on a warmer machine.
	$order = if($r % 2) { @($false, $true) } else { @($true, $false) }
	foreach($hold in $order)
	{
		$value = Invoke-Bench $hold
		if($hold) { $on += $value } else { $off += $value }
		Write-Host ("run {0}/{1} {2,-9} realtime={3:N1}%" -f $r, $Runs, ($(if($hold) { "hold $HoldUs" } else { 'no hold' })), $value)
	}
}

$mOff = Get-Median $off
$mOn = Get-Median $on
Write-Host ''
Write-Host ("median no hold    {0:N1}%  (min {1:N1}, max {2:N1})" -f $mOff, ($off | Measure-Object -Minimum).Minimum, ($off | Measure-Object -Maximum).Maximum)
Write-Host ("median hold {0,-5} {1:N1}%  (min {2:N1}, max {3:N1})" -f $HoldUs, $mOn, ($on | Measure-Object -Minimum).Minimum, ($on | Measure-Object -Maximum).Maximum)
Write-Host ("cost {0:+0.0;-0.0} points (rule: enable if <= ~5)" -f ($mOn - $mOff))
