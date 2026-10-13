extends SceneTree
func _init():
await process_frame
var vp := root
var found := false
for p in vp.get_property_list():
if "skin" in String(p.name):
print("VP_PROP: " + String(p.name))
found = true
print("skin_prop_found=" + str(found))
quit()
