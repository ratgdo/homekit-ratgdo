The release process
===

Once a release is ready, all you have to do is create a GitHub Release and tag. Once that release is created it will kick off the `.github/workflows/manifest.yml` GitHub Action. This workflow will do all the work for you to get a new release out. It will create an updated manifest.json file with the new version from the tag that you ceated with the release. Once that workflow completes it then triggers another workflow, `.github/workflows/build.yml`, this workflow will create the firmware.bin file following the format of `homekit-ratgdo-${{ latest-tag }}.bin`.

When the firmware and manifest.json is updated, this will automatically update the flasher page as the /docs folder is what the flasher page looks at so no need to update anything in this folder.

Releases tag should be named v[0-9]+.[0-9]+.[0-9], example: `v0.7.0`. the workflows are designed to follows this pattern, if this pattern isn't followed then the workflow will stop.
