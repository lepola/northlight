# Cache compatibility revisions

`scripts/install_world_cache.py` defines explicit `CACHE_VERSIONS` for scene,
lights, fog, celestial assets, instance metadata and validation. They are recorded
in cache inputs, prebuilt variant manifests and installer `BUILD-INFO.json`.

A DLL release, rebuild, source edit or source hash change does not invalidate a
versioned cache. Source hashes remain provenance. The client archive chain,
locale, selected maps and tiles still identify the data being generated.

When generated data or its interpretation changes, increment the affected
revision. A scene revision rebuilds geometry and its dependent lights/fog; a
light, fog or celestial revision rebuilds only that step. Instance groups track
scene and light revisions separately, preserving geometry on a light-only update.
Validation-only instance updates run the validator without rebuilding geometry.

Revision 1 introduces this scheme together with native fixture ownership and
flame-origin data. Unversioned caches rebuild once. Historical source-equivalence
rules cannot mark an unversioned cache current. A missing native-fixture catalog
is repaired by rebuilding lights, even if the revision is unchanged.

`test_install_world_cache.py` verifies unchanged files and timestamps after a
source-only reinstall, explicit revision changes, legacy migration, incomplete
data repair, instance reuse and interrupted swaps. `test_install_delivery.py`
checks version compatibility for prebuilt variants and complete package imports.
