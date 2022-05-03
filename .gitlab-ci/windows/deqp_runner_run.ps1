$dxil_dll = cmd.exe /C "C:\BuildTools\Common7\Tools\VsDevCmd.bat -host_arch=amd64 -arch=amd64 -no_logo && where dxil.dll" 2>&1
if ($dxil_dll -notmatch "dxil.dll$") {
    Write-Output "Couldn't get path to dxil.dll"
    exit 1
}
$env:Path = "$(Split-Path $dxil_dll);$env:Path"

# VK_ICD_FILENAMES environment variable is not used when running with
# elevated privileges. Add a key to the registry instead.
$hkey_path = "HKLM:\SOFTWARE\Khronos\Vulkan\Drivers\"
$hkey_name = Join-Path -Path $pwd -ChildPath "_install\share\vulkan\icd.d\dzn_icd.x86_64.json"
New-Item -Path $hkey_path -force
New-ItemProperty -Path $hkey_path -Name $hkey_name -Value 0 -PropertyType DWORD

$results = New-Item -ItemType Directory results
$deqp_options = @("--deqp-surface-width", 256, "--deqp-surface-height", 256, "--deqp-surface-type", "pbuffer", "--deqp-gl-config-name", "rgba8888d24s8ms0", "--deqp-visibility", "hidden")
$deqp_module = "C:\deqp\external\vulkancts\modules\vulkan\deqp-vk.exe"
$caselist = "C:\deqp\mustpass\vk-master.txt"
$baseline = ".\_install\warp-fails.txt"
$includes = @("-t", "dEQP-VK.api.*", "-t", "dEQP-VK.info.*", "-t", "dEQP-VK.draw.*", "-t", "dEQP-VK.query_pool.*", "-t", "dEQP-VK.memory.*")

$env:DZN_DEBUG = "warp"
deqp-runner run --deqp $($deqp_module) --output $($results) --caselist $($caselist) --baseline $($baseline) $($includes) --testlog-to-xml C:\deqp\executor\testlog-to-xml.exe --jobs 4 -- $($deqp_options)
$deqpstatus = $?

$template = "See https://$($env:CI_PROJECT_ROOT_NAMESPACE).pages.freedesktop.org/-/$($env:CI_PROJECT_NAME)/-/jobs/$($env:CI_JOB_ID)/artifacts/results/{{testcase}}.xml"
deqp-runner junit --testsuite dEQP --results "$($results)/failures.csv" --output "$($results)/junit.xml" --limit 50 --template $template

if (!$deqpstatus) {
    Exit 1
}
