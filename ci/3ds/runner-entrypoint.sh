#!/usr/bin/env bash
set -euo pipefail

RUNNER_HOME="/actions-runner"
RUNNER_WORKDIR="${RUNNER_WORKDIR:-/_work}"
RUNNER_LABELS="${RUNNER_LABELS:-3ds,devkitpro,devkitarm}"
RUNNER_NAME="${RUNNER_NAME:-ppsspp-3ds-${HOSTNAME}}"
RUNNER_EPHEMERAL="${RUNNER_EPHEMERAL:-false}"

log() {
  printf '[runner] %s\n' "$*"
}

repo_slug_from_url() {
  local url="$1"
  url="${url%.git}"
  url="${url#https://github.com/}"
  url="${url#git@github.com:}"
  printf '%s\n' "$url"
}

require_env() {
  if [[ -z "${!1:-}" ]]; then
    printf '[runner] missing required environment variable: %s\n' "$1" >&2
    exit 2
  fi
}

get_repo_token() {
  local token_kind="$1"
  local repo_slug="${GITHUB_REPOSITORY:-}"
  if [[ -z "${repo_slug}" ]]; then
    repo_slug="$(repo_slug_from_url "${REPO_URL}")"
  fi
  require_env GH_PAT
  curl -fsSL \
    -X POST \
    -H "Accept: application/vnd.github+json" \
    -H "Authorization: Bearer ${GH_PAT}" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "https://api.github.com/repos/${repo_slug}/actions/runners/${token_kind}" |
    jq -r '.token'
}

cd "${RUNNER_HOME}"

require_env REPO_URL

if [[ -n "${RUNNER_TOKEN:-}" ]]; then
  CONFIG_TOKEN="${RUNNER_TOKEN}"
else
  log "Requesting a repository runner registration token"
  CONFIG_TOKEN="$(get_repo_token registration-token)"
fi

if [[ -z "${CONFIG_TOKEN}" || "${CONFIG_TOKEN}" == "null" ]]; then
  printf '[runner] could not obtain a runner registration token\n' >&2
  exit 3
fi

cleanup() {
  log "Removing runner registration"
  local remove_token="${CONFIG_TOKEN}"
  if [[ -n "${GH_PAT:-}" ]]; then
    remove_token="$(get_repo_token remove-token || printf '%s' "${CONFIG_TOKEN}")"
  fi
  ./config.sh remove --unattended --token "${remove_token}" || true
}
trap cleanup EXIT INT TERM

config_args=(
  --unattended
  --replace
  --url "${REPO_URL}"
  --token "${CONFIG_TOKEN}"
  --name "${RUNNER_NAME}"
  --labels "${RUNNER_LABELS}"
  --work "${RUNNER_WORKDIR}"
)

if [[ "${RUNNER_EPHEMERAL}" == "true" ]]; then
  config_args+=( --ephemeral )
fi

log "Configuring ${RUNNER_NAME} for ${REPO_URL} with labels ${RUNNER_LABELS}"
./config.sh "${config_args[@]}"

log "Starting runner"
exec ./run.sh
