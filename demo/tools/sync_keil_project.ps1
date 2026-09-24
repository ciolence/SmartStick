<#
  sync_keil_project.ps1
  ------------------------------------------------------------------
  作用：把 Core/{Src,Inc}/user/** 下的全部源码/头文件登记进 Keil 工程，
        并补齐 include 路径；同时保证 Hardware / System 两个既有分组存在。

  特点：
    * 可重复执行（幂等）——分组内容每次按磁盘实际文件重建，不会重复添加；
    * CubeMX 重新生成工程后，重跑一次即可恢复（Keil 里不用手点）；
    * 会先备份 *.uvprojx.bak（已被 .gitignore 排除）。

  ⚠ 两个已踩过的坑（勿回退）：
    1) 写文件不能带 UTF-8 BOM —— Keil 工程解析器不接受，UV4 -b 会直接退出码 15。
    2) 不能用 StringWriter 保存 —— 它会把 XML 声明写成 encoding="utf-16"，
       而文件其实是 UTF-8，下次 XmlDocument.Load 会直接报错。
       必须用 XmlWriter + UTF8Encoding($false)。

  用法（在任意目录）：
    powershell -ExecutionPolicy Bypass -File f:\RM\smart_sti\demo\tools\sync_keil_project.ps1
#>

$ErrorActionPreference = 'Stop'

$demoRoot   = Split-Path -Parent $PSScriptRoot                    # ...\demo
$projRoot   = Join-Path $demoRoot 'HAL_SMART_STICK'               # 工程根
$mdkDir     = Join-Path $projRoot 'MDK-ARM'
$projPath   = Join-Path $mdkDir 'HAL_OLED.uvprojx'

if (-not (Test-Path $projPath)) { throw "找不到 Keil 工程文件：$projPath" }

# ---------- 1. 需要注册进工程的 include 路径 ----------
$incPaths = @(
  '../Core/Inc/user',
  '../Core/Inc/user/bsp',
  '../Core/Inc/user/drv',
  '../Core/Inc/user/svc',
  '../Core/Inc/user/app'
)

# ---------- 2. 分组定义（源目录 / 头目录，相对工程根） ----------
$groupDefs = @(
  @{ Name = 'User/Common'; SrcDir = 'Core\Src\user';     IncDir = ''                 },
  @{ Name = 'User/Bsp';    SrcDir = 'Core\Src\user\bsp'; IncDir = 'Core\Inc\user\bsp' },
  @{ Name = 'User/Drv';    SrcDir = 'Core\Src\user\drv'; IncDir = 'Core\Inc\user\drv' },
  @{ Name = 'User/Svc';    SrcDir = 'Core\Src\user\svc'; IncDir = 'Core\Inc\user\svc' },
  @{ Name = 'User/App';    SrcDir = 'Core\Src\user\app'; IncDir = 'Core\Inc\user\app' }
)

# ---------- 3. 用户既有文件（CubeMX 若丢失分组，本脚本会补回） ----------
$legacyDefs = @(
  @{ Name = 'Hardware'; Files = @(
      'Core\Src\OLED.c', 'Core\Inc\OLED.h', 'Core\Inc\OLED_Font.h',
      'Core\Src\Key.c',  'Core\Inc\Key.h' ) },
  @{ Name = 'System';   Files = @(
      'Core\Src\Delay.c', 'Core\Inc\Delay.h' ) }
)

# ---------- 工具函数 ----------
function Get-RelPath([string]$absPath) {
  # 工程内所有文件都在 $projRoot 之下 → 相对 MDK-ARM 目录就是 ..\ + 相对工程根的路径
  $rel = $absPath.Substring($projRoot.Length + 1)
  return '..\' + $rel
}

function New-FileNode($xml, [string]$fileName, [string]$fileType, [string]$filePath) {
  $f  = $xml.CreateElement('File')
  $fn = $xml.CreateElement('FileName');  $fn.InnerText = $fileName;  [void]$f.AppendChild($fn)
  $ft = $xml.CreateElement('FileType');  $ft.InnerText = $fileType;  [void]$f.AppendChild($ft)
  $fp = $xml.CreateElement('FilePath');  $fp.InnerText = $filePath;  [void]$f.AppendChild($fp)
  return $f
}

function Get-FileType([string]$path) {
  switch ([System.IO.Path]::GetExtension($path).ToLower()) {
    '.c'  { return '1' }
    '.h'  { return '5' }
    '.s'  { return '2' }
    default { return '5' }
  }
}

function Sync-Group($xml, $groupsNode, [string]$groupName, [array]$fileSpecs) {
  $group = $groupsNode.SelectSingleNode("Group[GroupName='$groupName']")
  $created = $false
  if (-not $group) {
    $group = $xml.CreateElement('Group')
    $gn = $xml.CreateElement('GroupName'); $gn.InnerText = $groupName
    [void]$group.AppendChild($gn)
    [void]$groupsNode.AppendChild($group)
    $created = $true
  }

  $filesNode = $group.SelectSingleNode('Files')
  if (-not $filesNode) {
    $filesNode = $xml.CreateElement('Files')
    [void]$group.AppendChild($filesNode)
  }
  $filesNode.RemoveAll()      # 按磁盘实际情况重建，保证幂等

  $count = 0
  foreach ($spec in $fileSpecs) {
    $abs = Join-Path $projRoot $spec
    if (-not (Test-Path $abs)) { continue }
    $fn = [System.IO.Path]::GetFileName($abs)
    [void]$filesNode.AppendChild((New-FileNode $xml $fn (Get-FileType $abs) (Get-RelPath $abs)))
    $count++
  }
  $mark = if ($created) { '+' } else { '=' }
  Write-Host ("  {0} {1} ：{2} 个文件" -f $mark, $groupName, $count)
}

# ---------- 主流程 ----------
Write-Host "工程文件：$projPath"

Copy-Item $projPath ($projPath + '.bak') -Force
Write-Host "已备份 → HAL_OLED.uvprojx.bak"

$xml = New-Object System.Xml.XmlDocument
$xml.PreserveWhitespace = $true
# 注意：不用 $xml.Load(路径)，因为读取时若声明与实际编码不符会抛异常
$xml.LoadXml([System.IO.File]::ReadAllText($projPath, [System.Text.Encoding]::UTF8))

# 4.1 include 路径
$incNode = $xml.SelectNodes('//Cads/VariousControls/IncludePath') |
           Where-Object { $_.InnerText -like '*Core/Inc*' } |
           Select-Object -First 1
if (-not $incNode) { throw "没找到主 IncludePath 节点，工程结构可能已变，请检查" }

$parts = @($incNode.InnerText -split ';' | Where-Object { $_ -ne '' })
foreach ($p in $incPaths) {
  if ($parts -notcontains $p) { $parts += $p }
}
$incNode.InnerText = ($parts -join ';')
Write-Host "  = IncludePath 已补齐（含 user 的 5 个子目录）"

# 4.2 分组
$groupsNode = $xml.SelectSingleNode('/Project/Targets/Target/Groups')
if (-not $groupsNode) { throw "没找到 <Groups> 节点，工程结构可能已变，请检查" }

foreach ($g in $groupDefs) {
  $specs = @()
  $srcAbs = Join-Path $projRoot $g.SrcDir
  if (Test-Path $srcAbs) {
    $specs += (Get-ChildItem $srcAbs -File -Filter '*.c' | Sort-Object Name |
               ForEach-Object { $g.SrcDir + '\' + $_.Name })
  }
  if ($g.IncDir -ne '') {
    $incAbs = Join-Path $projRoot $g.IncDir
    if (Test-Path $incAbs) {
      $specs += (Get-ChildItem $incAbs -File -Filter '*.h' | Sort-Object Name |
                 ForEach-Object { $g.IncDir + '\' + $_.Name })
    }
  }
  Sync-Group $xml $groupsNode $g.Name $specs
}

foreach ($g in $legacyDefs) {
  Sync-Group $xml $groupsNode $g.Name $g.Files
}

# 4.3 保存：UTF-8 无 BOM + 正确的 XML 声明
$settings = New-Object System.Xml.XmlWriterSettings
$settings.Encoding = New-Object System.Text.UTF8Encoding($false)
$settings.Indent = $false
$settings.OmitXmlDeclaration = $false
$writer = [System.Xml.XmlWriter]::Create($projPath, $settings)
try {
  $xml.Save($writer)
} finally {
  $writer.Close()
}

# 4.4 自检：确认无 BOM
$bytes = [System.IO.File]::ReadAllBytes($projPath)
$bom = ($bytes[0] -eq 0xEF) -and ($bytes[1] -eq 0xBB) -and ($bytes[2] -eq 0xBF)
Write-Host ("完成：{0}" -f $projPath)
Write-Host ("自检：BOM = {0}（必须为 False）" -f $bom)
Write-Host "提示：回到 Keil 若提示工程被外部修改，选择 Reload 即可。"
