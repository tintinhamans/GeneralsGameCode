$patchDirectoryPath = "Patch"  
$buildDir = "build\win32-vcpkg\GeneralsMD\Release";
$buildVer = 1;

$crcFilesDir = "$patchDirectoryPath/private/genonlineserver/crcfiles"
$installerDir = "$patchDirectoryPath/public_html"
$updaterDir = "$patchDirectoryPath/public_html/updater"
$workingDir = "$patchDirectoryPath/working_dir"

# The audio notice must name the exact commit that the LGPL source offer points to
$notice = Get-Content -Raw -Path "$buildDir\audio-libraries-NOTICE.txt"
if ($notice -notmatch "built from (tag \S+, )?commit [0-9a-f]{40}\r?\n")
{
   throw "audio-libraries-NOTICE.txt names no exact commit. Build from a clean checkout before packaging."
}

# Check if the patch directory exists  
if (Test-Path $patchDirectoryPath)
{  
   # If it exists, delete all contents  
   Get-ChildItem -Path $patchDirectoryPath -Recurse -Force | Remove-Item -Recurse -Force  
}
else
{  
   # If it does not exist, create the directory  
   New-Item -ItemType Directory -Path $patchDirectoryPath
}

# make our folder structure
New-Item -ItemType Directory -Path "$crcFilesDir"
New-Item -ItemType Directory -Path "$updaterDir"
New-Item -ItemType Directory -Path "$workingDir"

# Copy runtime libraries and release exe to working dir
Copy-Item -Path "$buildDir\CrashReportHandler.exe" -Destination $workingDir
Copy-Item -Path "$buildDir\discord-rpc.dll" -Destination $workingDir
Copy-Item -Path "$buildDir\discord-rpc-LICENSE.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\genonline_zerohour.exe" -Destination $workingDir

# Copy the license texts that ship with the exe
Copy-Item -Path "$buildDir\THIRD_PARTY_NOTICES.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\LICENSE-GPL3.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\audio-libraries-NOTICE.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\ffmpeg-LICENSE.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\openal-soft-LICENSE.txt" -Destination $workingDir
Copy-Item -Path "$buildDir\openal-soft-pffft-LICENSE.txt" -Destination $workingDir
Copy-Item -Path "GenOnline\GameData\Fonts\IBMPlex-LICENSE.txt" -Destination $workingDir

# package up our patch and installer
Compress-Archive -Path "$workingDir\*" -DestinationPath "$workingDir\v$buildVer.zip"

# Copy the patch to the patch directory  
Copy-Item -Path "$workingDir\v$buildVer.zip" -Destination "$installerDir\v$buildVer.zip"
Copy-Item -Path "$workingDir\v$buildVer.zip" -Destination "$updaterDir\v$buildVer.gopatch"

# The LGPL source offer for the statically linked FFmpeg and OpenAL Soft is published next to the patch download
$vcpkgDownloads = if ($env:VCPKG_DOWNLOADS) { $env:VCPKG_DOWNLOADS } else { "$env:LOCALAPPDATA\vcpkg\downloads" }
$sourcesDir = "$patchDirectoryPath/sources_dir"
New-Item -ItemType Directory -Path "$sourcesDir/ports"

# Takes the archive from the vcpkg download cache or from its URL and checks it against the SHA512 in the port
function Copy-PortArchive([string]$portDir, [string]$fileName, [string]$url)
{
   $sha512 = [regex]::Match((Get-Content -Raw -Path "$portDir\portfile.cmake"), "SHA512\s+([0-9a-f]{128})").Groups[1].Value
   $target = "$sourcesDir\$fileName"
   if (Test-Path "$vcpkgDownloads\$fileName")
   {
      Copy-Item -Path "$vcpkgDownloads\$fileName" -Destination $target
   }
   else
   {
      Invoke-WebRequest -Uri $url -OutFile $target
   }
   if ((Get-FileHash -Algorithm SHA512 -Path $target).Hash.ToLower() -ne $sha512)
   {
      throw "$fileName does not match the SHA512 in $portDir\portfile.cmake"
   }
}

$ffmpegVersion = (Get-Content -Raw -Path "cmake\ports\ffmpeg\vcpkg.json" | ConvertFrom-Json).version
$openalVersion = (Get-Content -Raw -Path "cmake\ports\openal-soft\vcpkg.json" | ConvertFrom-Json).version
Copy-PortArchive "cmake\ports\ffmpeg" "ffmpeg-ffmpeg-n$ffmpegVersion.tar.gz" "https://github.com/ffmpeg/ffmpeg/archive/n$ffmpegVersion.tar.gz"
Copy-PortArchive "cmake\ports\openal-soft" "openal-soft-$openalVersion.tar.gz" "https://github.com/kcat/openal-soft/archive/refs/tags/$openalVersion.tar.gz"
Copy-Item -Path "cmake\ports\ffmpeg", "cmake\ports\openal-soft" -Destination "$sourcesDir/ports" -Recurse
Copy-Item -Path "vcpkg.json" -Destination $sourcesDir
Copy-Item -Path "$buildDir\audio-libraries-NOTICE.txt" -Destination $sourcesDir
Copy-Item -Path "$buildDir\ffmpeg-LICENSE.txt", "$buildDir\openal-soft-LICENSE.txt", "$buildDir\openal-soft-pffft-LICENSE.txt" -Destination $sourcesDir

Compress-Archive -Path "$sourcesDir\*" -DestinationPath "$installerDir\v$buildVer-sources.zip"
Remove-Item -Path $sourcesDir -Recurse -Force

# copy the exe to crcfiles
Copy-Item -Path "$buildDir\genonline_zerohour.exe" -Destination "$crcFilesDir"

# cleanup working dir
if (Test-Path $workingDir)
{  
   # If it exists, delete all contents  
   Get-ChildItem -Path $workingDir -Recurse -Force | Remove-Item -Recurse -Force  

   # Remove the working directory itself
   Remove-Item -Path $workingDir -Recurse -Force
}
