import { readFile } from 'node:fs/promises';

// ESP Web Tools 10.4.0 has no public success-screen hook. Add a small,
// build-time rendering hook; leave flashing, errors and serial cleanup intact.
export function patchInstallDialog(source) {
  const guard = `
        if (this._installConfirmed && this._installState?.state === "finished" &&
            this._client !== undefined && this.overrides?.renderInstallSuccess) {
            return this.overrides.renderInstallSuccess({
                close: () => this._closeDialog(),
                showLogs: () => { this._client = undefined; this._state = "LOGS"; },
            });
        }`;
  for (const method of ['_renderInstall', '_renderDashboardNoImprov']) {
    const marker = `    ${method}() {`;
    if (source.split(marker).length !== 2) {
      throw new Error(`ESP Web Tools success hook needs review: ${method}`);
    }
    source = source.replace(marker, marker + guard);
  }
  // Saving evidence must not reboot the board being diagnosed.
  const downloadAndReset = `textDownload(this.shadowRoot.querySelector("ewt-console").logs(), \`esp-web-tools-logs.txt\`);
            this.shadowRoot.querySelector("ewt-console").reset();`;
  if (source.split(downloadAndReset).length !== 2) {
    throw new Error('ESP Web Tools log-download hook needs review');
  }
  source = source.replace(downloadAndReset, downloadAndReset.split('\n')[0]);
  return source;
}

export const installDialogPatch = {
  name: 'dashbridge-install-success',
  setup(builder) {
    builder.onLoad({ filter: /[/\\]esp-web-tools[/\\]dist[/\\]install-dialog\.js$/ }, async ({ path }) => {
      const metadata = JSON.parse(await readFile(new URL('./node_modules/esp-web-tools/package.json', import.meta.url), 'utf8'));
      if (metadata.version !== '10.4.0') throw new Error('Review success hook before upgrading ESP Web Tools');
      return { contents: patchInstallDialog(await readFile(path, 'utf8')), loader: 'js' };
    });
  },
};
