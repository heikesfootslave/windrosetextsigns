const MAX_BODY_BYTES = 64 * 1024;
const MAX_REQUESTS = 256;

function json(data, init = {}) {
  return new Response(JSON.stringify(data), {
    ...init,
    headers: {
      "content-type": "application/json; charset=utf-8",
      ...(init.headers || {})
    }
  });
}

async function readJson(request) {
  const size = Number(request.headers.get("content-length") || "0");
  if (size > MAX_BODY_BYTES) {
    throw new Error("payload_too_large");
  }
  return await request.json();
}

function requireAuth(request, env) {
  const secret = env.WTS_RELAY_SHARED_SECRET;
  if (!secret) {
    return true;
  }
  const header = request.headers.get("authorization") || "";
  return header === `Bearer ${secret}`;
}

function routeParts(url) {
  return new URL(url).pathname.split("/").filter(Boolean);
}

export class WTSRoom {
  constructor(state, env) {
    this.state = state;
    this.env = env;
  }

  async fetch(request) {
    if (!requireAuth(request, this.env)) {
      return json({ ok: false, error: "unauthorized" }, { status: 401 });
    }

    const url = new URL(request.url);
    const parts = routeParts(request.url);
    const role = parts[3] || "";
    const action = parts[4] || "";

    if (request.method === "GET" && role === "client" && action === "snapshot") {
      const snapshot = await this.state.storage.get("snapshot");
      return json({
        ok: true,
        snapshot: snapshot || null,
        serverSeenAt: (await this.state.storage.get("serverSeenAt")) || null
      });
    }

    if (request.method === "POST" && role === "server" && action === "snapshot") {
      const snapshot = await readJson(request);
      const now = new Date().toISOString();
      await this.state.storage.put("snapshot", {
        ...snapshot,
        relayedAt: now
      });
      await this.state.storage.put("serverSeenAt", now);
      return json({ ok: true });
    }

    if (request.method === "POST" && role === "client" && action === "requests") {
      const body = await readJson(request);
      const nextSeq = ((await this.state.storage.get("nextRequestSeq")) || 1);
      const item = {
        seq: nextSeq,
        receivedAt: new Date().toISOString(),
        body
      };
      const requests = (await this.state.storage.get("requests")) || [];
      requests.push(item);
      while (requests.length > MAX_REQUESTS) {
        requests.shift();
      }
      await this.state.storage.put("requests", requests);
      await this.state.storage.put("nextRequestSeq", nextSeq + 1);
      return json({ ok: true, seq: nextSeq });
    }

    if (request.method === "GET" && role === "server" && action === "requests") {
      const after = Number(url.searchParams.get("after") || "0");
      const requests = (await this.state.storage.get("requests")) || [];
      return json({
        ok: true,
        requests: requests.filter((item) => item.seq > after)
      });
    }

    if (request.method === "GET" && role === "debug" && action === "status") {
      return json({
        ok: true,
        hasSnapshot: Boolean(await this.state.storage.get("snapshot")),
        serverSeenAt: (await this.state.storage.get("serverSeenAt")) || null,
        nextRequestSeq: (await this.state.storage.get("nextRequestSeq")) || 1,
        queuedRequests: ((await this.state.storage.get("requests")) || []).length
      });
    }

    return json({ ok: false, error: "not_found" }, { status: 404 });
  }
}

export default {
  async fetch(request, env) {
    const parts = routeParts(request.url);
    if (parts[0] === "health") {
      return json({ ok: true, service: "windrose-text-signs-relay" });
    }
    if (parts[0] !== "v1" || parts[1] !== "rooms" || !parts[2]) {
      return json({ ok: false, error: "not_found" }, { status: 404 });
    }

    const roomId = parts[2];
    const id = env.ROOMS.idFromName(roomId);
    const stub = env.ROOMS.get(id);
    return stub.fetch(request);
  }
};

