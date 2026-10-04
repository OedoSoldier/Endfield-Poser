"""Install only this add-on, preserving all other Blender preferences."""
from pathlib import Path
import tempfile
import zipfile
import bpy
import addon_utils

source = Path(__file__).resolve().parent / 'endfield_poser_bridge'
with tempfile.TemporaryDirectory(prefix='endfield-blender-') as work:
    archive = Path(work) / 'endfield_poser_bridge.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in source.iterdir():
            if path.suffix == '.py' or path.name == 'README.md':
                z.write(path, 'endfield_poser_bridge/' + path.name)
    bpy.ops.preferences.addon_install(filepath=str(archive), overwrite=True)
addon_utils.enable('endfield_poser_bridge', default_set=True, persistent=True)
assert addon_utils.check('endfield_poser_bridge')[1], 'Add-on did not enable'
bpy.ops.wm.save_userpref()
print('ENDFIELD_BRIDGE_INSTALLED', bpy.utils.user_resource('SCRIPTS', path='addons'))
