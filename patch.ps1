Write-Host "Download ns-allinone-3.44" -ForegroundColor Cyan
Invoke-WebRequest -Uri "https://www.nsnam.org/releases/ns-allinone-3.44.tar.bz2" -OutFile "ns-allinone-3.44.tar.bz2"

Write-Host "tar -xjf ns-allinone-3.44.tar.bz2" -ForegroundColor Cyan
tar -xjf ns-allinone-3.44.tar.bz2

Write-Host "mv ./ns-allinone-3.44/ns-3.44 ./ns3feat" -ForegroundColor Cyan
Rename-Item -Path "./ns-allinone-3.44/ns-3.44" -NewName "ns3feat"

Set-Location "./ns3feat"
Write-Host "Initialize repository" -ForegroundColor Cyan
Remove-Item -Recurse -Force -ErrorAction SilentlyContinue .git
git init
git add -A
git commit -m "Initial commit"
Set-Location ..

Write-Host "Git clone ns3.44" -ForegroundColor Cyan
git clone git@github.com:161043261/ns3.44.git

Write-Host "Apply patch" -ForegroundColor Cyan

Get-ChildItem -Path "./ns3.44/scratch/*.cc" | ForEach-Object {
  Copy-Item -Path $_.FullName -Destination "./ns3feat/scratch/" -Force
}

$filesToCopy = @(
  "src/internet/model/tcp-socket-base.h",
  "src/internet/model/tcp-socket-base.cc",
  "src/internet/model/tcp-bbr.h",
  "src/internet/model/tcp-bbr.cc",
  ".gitignore"
)

foreach ($file in $filesToCopy) {
  $source = "./ns3.44/$file"
  $destination = "./ns3feat/$file"

  $destDir = [System.IO.Path]::GetDirectoryName($destination)
  if (-not (Test-Path -Path $destDir)) {
    New-Item -ItemType Directory -Path $destDir | Out-Null
  }

  Copy-Item -Path $source -Destination $destination -Force
}

Set-Location "./ns3feat"
Write-Host "Commit patch" -ForegroundColor Cyan
git add -A
git commit -m "feat: Introduce new features"

Write-Host "Configure and build" -ForegroundColor Cyan
./ns3 configure
./ns3 build

Write-Host "Done!" -ForegroundColor Green
Set-Location ..
