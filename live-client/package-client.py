from pathlib import Path
import plistlib
app=Path(__file__).resolve().parent/'SWG Native Client.app'
info={'CFBundleExecutable':'SWGNativeClient','CFBundleIdentifier':'local.swg.nativeclient',
      'CFBundleName':'SWG Native Client','CFBundleDisplayName':'SWG Native Client','CFBundlePackageType':'APPL',
      'CFBundleShortVersionString':'0.2.24','CFBundleVersion':'26','LSMinimumSystemVersion':'26.0','NSHighResolutionCapable':True}
(app/'Contents/Info.plist').write_bytes(plistlib.dumps(info))

world=app/'Contents/Helpers/SWG Native World.app'
worldInfo=dict(info,CFBundleExecutable='SWGNativeWorld',CFBundleIdentifier='local.swg.nativeworld',CFBundleName='SWG Native World',CFBundleDisplayName='SWG Native World')
(world/'Contents/Info.plist').write_bytes(plistlib.dumps(worldInfo))
