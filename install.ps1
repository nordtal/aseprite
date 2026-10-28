# Installs Nordtal Designer on Windows from source, or updates it:
#
#   irm https://raw.githubusercontent.com/nordtal/aseprite/main/install.ps1 | iex
#
# It installs what the build needs that is missing (Git and the Visual
# Studio Build Tools through winget, CMake, Ninja and Skia), clones this
# repository or pulls it, builds the app, puts it into
# %LOCALAPPDATA%\Programs\Nordtal Designer and adds it to the start
# menu. A failed step names itself and leaves an installed app as it
# was.
#
# NORDTAL_DESIGNER_HOME is where the source, the tools and the build
# go (%USERPROFILE%\.nordtal-designer), NORDTAL_DESIGNER_BRANCH the
# branch it builds (main) and NORDTAL_DESIGNER_JOBS how many compilers
# run at once (as many as Ninja chooses).

# iex runs this in the caller's PowerShell, which an exit would close,
# so everything is inside a script block that returns instead
& {
  $ErrorActionPreference = 'Stop'
  $ProgressPreference = 'SilentlyContinue'

  $repo = 'https://github.com/nordtal/aseprite.git'
  $root = if ($env:NORDTAL_DESIGNER_HOME) { $env:NORDTAL_DESIGNER_HOME } else { Join-Path $env:USERPROFILE '.nordtal-designer' }
  $branch = if ($env:NORDTAL_DESIGNER_BRANCH) { $env:NORDTAL_DESIGNER_BRANCH } else { 'main' }
  $src = Join-Path $root 'source'
  $tools = Join-Path $root 'tools'
  $build = Join-Path $root 'build'
  $apps = Join-Path $env:LOCALAPPDATA 'Programs'
  $app = Join-Path $apps 'Nordtal Designer'

  $cmakeVersion = '3.31.8'
  $cmakeSha256 = '81aa9964dbabd71fe02e7ec50472fd3ad56138c49944515ece9001efbff8d719'
  $ninjaVersion = '1.12.1'
  $ninjaSha256 = 'f550fec705b6d6ff58f2db3c374c2277a37691678d6aba463adcbb129108467a'

  # Runs a program and throws if it fails
  function Run {
    & $args[0] @($args | Select-Object -Skip 1)
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) exited with $LASTEXITCODE" }
  }

  function Fetch($url, $sha256, $file) {
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile "$file.part"
    if ($sha256 -and (Get-FileHash -Algorithm SHA256 "$file.part").Hash -ne $sha256) {
      throw "$url doesn't have the expected checksum"
    }
    Move-Item -Force "$file.part" $file
  }

  # The PATH of a new shell, which a winget install changes
  function Update-Path {
    $env:Path = [Environment]::GetEnvironmentVariable('Path', 'Machine') + ';' +
                [Environment]::GetEnvironmentVariable('Path', 'User')
  }

  # winget.exe by its full name, which this function's name would shadow
  function Winget($id, $override) {
    $more = if ($override) { @('--override', $override) } else { @() }
    Run winget.exe install --exact --id $id --silent --accept-package-agreements --accept-source-agreements @more
    Update-Path
  }

  $step = $null
  try {
    New-Item -ItemType Directory -Force $tools | Out-Null

    $step = 'Git'; Write-Host "==> $step"
    if (-not (Get-Command git.exe -ErrorAction SilentlyContinue)) {
      Winget Git.Git
    }
    # The bash of Git for Windows, wherever the git.exe on the PATH lives
    $gitRoot = (Run git.exe --exec-path) -replace '/mingw64/.*$', '' -replace '/', '\'
    $bash = Join-Path $gitRoot 'bin\bash.exe'
    if (-not (Test-Path $bash)) { throw "Git for Windows has no bash at $bash" }

    $step = 'Visual Studio Build Tools'; Write-Host "==> $step"
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    function Find-VS {
      if (Test-Path $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
      }
    }
    $vs = Find-VS
    if (-not $vs) {
      # A Visual Studio without the C++ workload gets it added, which
      # winget can't do to an installed package
      $other = if (Test-Path $vswhere) { & $vswhere -latest -products * -property installationPath }
      if ($other) {
        $setup = Start-Process -Verb RunAs -Wait -PassThru (Join-Path (Split-Path $vswhere) 'setup.exe') `
          "modify --installPath `"$other`" --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --norestart"
        if ($setup.ExitCode -ne 0) { throw "the Visual Studio Installer exited with $($setup.ExitCode)" }
      } else {
        Winget Microsoft.VisualStudio.2022.BuildTools '--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended'
      }
      $vs = Find-VS
      if (-not $vs) { throw 'the C++ build tools are not installed' }
    }
    # The compiler's environment, as the Developer Command Prompt sets it
    $vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
    cmd.exe /c "`"$vcvars`" > nul && set" | ForEach-Object {
      if ($_ -match '^([^=]+)=(.*)$') { Set-Item "env:$($Matches[1])" $Matches[2] }
    }
    if ($LASTEXITCODE -ne 0) { throw "$vcvars failed" }

    $step = 'CMake'; Write-Host "==> $step"
    $cmakeDir = Join-Path $tools "cmake-$cmakeVersion"
    $cmake = Join-Path $cmakeDir 'bin\cmake.exe'
    if (-not (Test-Path $cmake)) {
      $zip = Join-Path $tools 'cmake.zip'
      Fetch "https://github.com/Kitware/CMake/releases/download/v$cmakeVersion/cmake-$cmakeVersion-windows-x86_64.zip" $cmakeSha256 $zip
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $cmakeDir, "$tools\cmake-$cmakeVersion-windows-x86_64"
      Expand-Archive $zip $tools
      Rename-Item "$tools\cmake-$cmakeVersion-windows-x86_64" $cmakeDir
      Remove-Item $zip
    }

    $step = 'Ninja'; Write-Host "==> $step"
    $ninjaDir = Join-Path $tools "ninja-$ninjaVersion"
    $ninja = Join-Path $ninjaDir 'ninja.exe'
    if (-not (Test-Path $ninja)) {
      $zip = Join-Path $tools 'ninja.zip'
      Fetch "https://github.com/ninja-build/ninja/releases/download/v$ninjaVersion/ninja-win.zip" $ninjaSha256 $zip
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $ninjaDir
      Expand-Archive $zip $ninjaDir
      Remove-Item $zip
    }

    $step = 'Source'; Write-Host "==> $step"
    if (Test-Path (Join-Path $src '.git')) {
      Run git -C $src fetch --quiet origin $branch
      Run git -C $src checkout --quiet --force -B $branch FETCH_HEAD
      Run git -C $src submodule sync --quiet --recursive
      Run git -C $src submodule update --quiet --init --recursive
    } else {
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $src
      Run git clone --quiet --recursive --branch $branch $repo $src
    }

    $step = 'Skia'; Write-Host "==> $step"
    $skiaTag = (Get-Content (Join-Path $src 'laf\misc\skia-tag.txt') -Raw).Trim()
    $skia = Join-Path $tools "skia-$skiaTag-x64"
    if (-not (Test-Path (Join-Path $skia 'out\Release-x64'))) {
      $zip = Join-Path $tools 'skia.zip'
      Fetch "https://github.com/aseprite/skia/releases/download/$skiaTag/Skia-Windows-Release-x64.zip" $null $zip
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $skia
      Expand-Archive $zip $skia
      Remove-Item $zip
    }

    $step = 'Build'; Write-Host "==> $step"
    Run $cmake -S $src -B $build -G Ninja `
      "-DCMAKE_MAKE_PROGRAM=$ninja" `
      -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      -DENABLE_CCACHE=off `
      -DLAF_BACKEND=skia `
      "-DSKIA_DIR=$skia" `
      "-DSKIA_LIBRARY_DIR=$skia\out\Release-x64"
    $jobs = if ($env:NORDTAL_DESIGNER_JOBS) { @('-j', $env:NORDTAL_DESIGNER_JOBS) } else { @() }
    Run $ninja -C $build @jobs aseprite

    $step = 'Bootstrap tools'; Write-Host "==> $step"
    # The app is put together next to the old one first, so a failed
    # step leaves the old one in place
    $new = "$app.new"
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $new
    New-Item -ItemType Directory -Force $new | Out-Null
    Copy-Item (Join-Path $build 'bin\Nordtal Designer.exe') $new
    Copy-Item -Recurse (Join-Path $build 'bin\data') $new
    Run $bash (Join-Path $src 'misc/designer/fetch-tools.sh') windows (Join-Path $new 'data')

    $step = 'Install'; Write-Host "==> $step"
    $old = "$app.old"
    # A run stopped mid-swap left the app only as .old
    if (-not (Test-Path $app) -and (Test-Path $old)) { Rename-Item $old $app }
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $old
    # A running app can't be moved, which stops here
    if (Test-Path $app) { Rename-Item $app $old }
    try { Rename-Item $new $app } catch {
      if (Test-Path $old) { Rename-Item $old $app }
      throw
    }
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $old
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut(
      (Join-Path ([Environment]::GetFolderPath('Programs')) 'Nordtal Designer.lnk'))
    $shortcut.TargetPath = Join-Path $app 'Nordtal Designer.exe'
    $shortcut.WorkingDirectory = $app
    $shortcut.Save()

    Write-Host "Nordtal Designer is installed in $app and in the start menu. Run the same line again to update it."
    $global:LASTEXITCODE = 0
  } catch {
    Write-Host "Nordtal Designer: the step `"$step`" failed. The installed app is unchanged." -ForegroundColor Red
    Write-Host $_ -ForegroundColor Red
    # For a caller that checks, such as the install workflow
    $global:LASTEXITCODE = 1
  }
}
