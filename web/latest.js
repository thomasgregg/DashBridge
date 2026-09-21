const versionPattern = /^\d+\.\d+\.\d+(?:-[A-Za-z0-9]+(?:\.[A-Za-z0-9]+)*)?\+[0-9a-f]{12}$/;

export function applyLatest(choices, latest) {
  if (!latest || typeof latest !== 'object') throw new Error('Invalid latest firmware response');
  for (const choice of choices) {
    const role = choice.value;
    const entry = latest[role];
    const manifestPattern = new RegExp(`^\\./firmware/${role}-[0-9a-f]{16}\\.json$`);
    if (!entry || !versionPattern.test(entry.version) || !manifestPattern.test(entry.manifest)) {
      throw new Error(`Invalid latest firmware entry for ${role}`);
    }
  }
  for (const choice of choices) {
    choice.dataset.version = latest[choice.value].version;
    choice.dataset.manifest = latest[choice.value].manifest;
  }
}

export async function refreshLatest(choices, fetcher, baseUrl) {
  const url = new URL('./latest.json', baseUrl);
  url.searchParams.set('cache', Date.now().toString());
  const response = await fetcher(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`Latest firmware request failed: ${response.status}`);
  applyLatest(choices, await response.json());
}
