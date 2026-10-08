# -*- mode: python ; coding: utf-8 -*-
from pathlib import Path
import os
import sys
import ssl
import urllib.request
import subprocess
import shutil
import errno
import certifi  #non-standard
from zipfile import ZipFile

#region Download File
def DownloadFile(url, dest, callback=None):
    # still do this on dryrun because it writes to temp?
    sslcontext = ssl.create_default_context(cafile=certifi.where())
    old_func = ssl._create_default_https_context
    ssl._create_default_https_context = lambda : sslcontext # HACK

    print('\n\ndownloading', url, 'to', dest)
    urllib.request.urlretrieve(url, dest, callback) # "legacy interface"
    print('done downloading ', url, 'to', dest)

    ssl._create_default_https_context = old_func

#endregion

#region Clean Directories
def CleanBuildDirectories(dirs):
    print("")
    print("Cleaning Build Directories")
    print("----------------------------------------------")
    for dir in dirs:
        print("Cleaning Directory: "+str(dir))
        try:
            shutil.rmtree(dir)
            print("    Cleaned")
        except OSError as e:
            if (e.errno==errno.ENOENT):
                print("    Already Clean")
            else:
                print("    Failed to clean directory: %s" % (e.strerror))
#endregion

#region Build DeusExe
def BuildDeusExe(basedir):
    result = False
    buildscript = basedir / 'build.ps1'
    print(buildscript)

    #Always build the Release configuration
    cmd = 'powershell.exe -file '+str(buildscript)+' -Configuration "Release"'

    p = subprocess.Popen(cmd, shell=True)
    stdout, stderr = p.communicate()

    if (p.returncode==0):
        print("Build Succeeded")
        result = True
    else:
        print("Build Failed!")
        result = False

    return result
#endregion

#region Collect Results
def CopyFile(srcFile,destFile):
    print(str(srcFile) + " ---> " + str(destFile))
    shutil.copy2(srcFile, destFile)


def CollectBuildResults(basedir):
    print("")
    print("Collecting build results...")

    releasedir = basedir / "Release"
    #Make sure the output directory exists (or create it if not)
    distdir = basedir / "dist"

    create = True
    if (os.path.exists(distdir)):
        if (os.path.isdir(distdir)):
            create=False
            print(str(distdir)+" exists already")
        else:
            print(str(distdir)+" exists, but it's a file???")
            distdir.unlink()

    if create:
        os.mkdir(distdir)
        print("Created "+str(distdir))

    CopyFile(releasedir / "deusex.exe", distdir / "DeusEx.exe")
    CopyFile(basedir / "SubtitleFix.u", distdir / "SubtitleFix.u")

    #Zip the contents up
    shutil.make_archive(basedir/'DeusExeModern','zip',distdir)
    print("Packaged DeusExeModern into ZIP")
    shutil.move(basedir / 'DeusExeModern.zip',distdir/'DeusExeModern.zip')

#endregion

#region Fetch Headers
def CheckHeaderExistence(headerfolder,gamename):
    testfile = headerfolder / gamename / 'Core' / 'Inc' / 'Core.h'
    return testfile.exists()

def ExtractZip(filename,outdir):
    if (filename.exists()==False):
        return False

    zip = ZipFile(filename, 'r')
    zip.extractall(outdir)
    zip.close()
    (filename).unlink()
    return True

def FetchGameHeaders(basedir):
    tmpdir = basedir / 'tmp'
    headersdir = tmpdir/'GameHeaders'
    DeusExHeaderDir = basedir / 'Games' / 'DeusEx'

    if CheckHeaderExistence(basedir / 'Games','DeusEx'):
        print("DeusEx headers already exist")
        return

    print("DeusEx headers not found.  Fetching...")

    if not tmpdir.exists():
        os.mkdir(tmpdir)

    #Actually download the header package
    headerdst = tmpdir / 'games.zip'
    url = "https://www.kentie.net/article/d3d10drv/files/src/games.zip"
    if (not headerdst.exists()):
        DownloadFile(url,headerdst)

    print("Extracting headers")
    if ExtractZip(headerdst,headersdir):
        shutil.copytree(headersdir/'Games'/'DeusEx', DeusExHeaderDir, dirs_exist_ok=True)
    else:
        print("Failed to extract headers?")
    
#endregion

#-----------------------------------------------#


#region Run Build

#The location of this python file
base = Path(sys.argv[0]).parents[0]


#Clean output directories
builddirs = []
builddirs.append(base / "Release") #Build product directory
builddirs.append(base / "Debug")   #Debug build product directory
builddirs.append(base /  "_work")  #compile stuff
builddirs.append(base /  "dist")  #distributable stuff
builddirs.append(base /  "tmp")  #temporary stuff
#Actually clean them
CleanBuildDirectories(builddirs)

FetchGameHeaders(base)

result = BuildDeusExe(base)

if (result==False):
    sys.exit(1)

CollectBuildResults(base)

#Quickly clean up the tmp directory
builddirs = []
builddirs.append(base /  "tmp")  #temporary stuff
CleanBuildDirectories(builddirs)

sys.exit(0)

#endregion
