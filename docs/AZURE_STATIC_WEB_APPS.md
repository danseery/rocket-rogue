# Azure Static Web Apps Deployment

Rocket Rogue is deployed as a static WebAssembly/WebGL app. Azure should receive only the generated game files, not the full CMake build directory.

## Azure target

- Service: Azure Static Web Apps
- Plan: Free
- Subscription ID: `<azure-subscription-id>`
- Tenant ID: `<azure-tenant-id>`
- Recommended resource group: `rg-rocket-rogue`
- Recommended app name: `rocket-rogue`
- Recommended region: `eastus2`
- GitHub branch: `main`

## One-time Azure setup

Create an Azure Static Web App in the Azure Portal or with Azure CLI. If using the portal, choose:

- Hosting plan: Free
- Deployment source: GitHub
- Repository: `<owner>/rocket-rogue`
- Branch: `main`
- Build presets/framework: Custom
- App location: `dist/azure-static-web-app`
- API location: empty
- Output location: empty

If Azure generates its own workflow file, do not keep both workflows. Use this repo's `.github/workflows/azure-static-web-app.yml` so the C++/Emscripten build and tests run before deployment.

## Required GitHub secret

The workflow uses the Azure Static Web Apps deployment token:

```text
AZURE_STATIC_WEB_APPS_API_TOKEN
```

Find it in the Azure Static Web App resource under **Manage deployment token**, then add it in GitHub:

```text
Repository -> Settings -> Secrets and variables -> Actions -> New repository secret
```

## What the workflow does

On pushes to `main`, GitHub Actions will:

1. Install the Ubuntu build dependencies and Emscripten `6.0.0`.
2. Configure and build the web target with CMake.
3. Run the web core tests and sanity check.
4. Copy only deployable files into `dist/azure-static-web-app`.
5. Upload that folder to Azure Static Web Apps.

The deploy package contains:

- `index.html`
- `rocket_rogue.html`
- `rocket_rogue.js`
- `rocket_rogue.wasm`
- `assets/`
- `staticwebapp.config.json`

## Local verification

From an activated dev shell:

> Codex desktop note: on Windows, the sandbox can block Ninja when running `cmake --build --preset web-release`. Codex agents should request escalated execution for that build command immediately instead of first attempting the sandboxed command.
>
> Codex desktop note: on Windows, use `npm.cmd` for npm scripts. Do not try `npm run ...` first; PowerShell can block the `npm.ps1` shim.
>
> Codex desktop note: for every Git command in this repo, set `$repo = (Get-Location).Path` and use `git -c safe.directory="$repo" ...`. Do not try plain `git ...` first; the sandbox user can trigger Git's dubious-ownership protection.

```powershell
cmake --preset web-release
cmake --build --preset web-release
ctest --preset web-release
npm.cmd run sanity
npm.cmd run prepare:azure
```

Known-good Codex commit, push, and deploy trigger flow:

```powershell
$repo = (Get-Location).Path
git -c safe.directory="$repo" status --short --branch
git -c safe.directory="$repo" diff --check
npm.cmd run sanity
git -c safe.directory="$repo" add --all
git -c safe.directory="$repo" commit -m "Commit message"
git -c safe.directory="$repo" push origin main
gh run list --repo <owner>/rocket-rogue --branch main --limit 3
```

Serve the deploy package locally:

```bash
node tools/serve.mjs dist/azure-static-web-app 8080
```

Open `http://localhost:8080/rocket_rogue.html`.
