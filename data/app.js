const POLL_INTERVAL_MS = 3000;
const MAX_MESSAGE_LENGTH = 200;
const MAX_STORED_MESSAGES = 100;
const STORAGE_KEY = "pocket-chatroom-user";

const state = {
  lastSnapshot: "",
  loading: false,
};

const elements = {
  connectionStatus: document.getElementById("connection-status"),
  chatForm: document.getElementById("chat-form"),
  historyNote: document.getElementById("history-note"),
  message: document.getElementById("message"),
  messageCount: document.getElementById("message-count"),
  messages: document.getElementById("messages"),
  messageTemplate: document.getElementById("message-template"),
  sendButton: document.getElementById("send-button"),
  username: document.getElementById("username"),
};

function getUsername() {
  const stored = (elements.username.value || "").trim();
  return stored || "Anonymous";
}

function setStatus(text) {
  elements.connectionStatus.textContent = text;
}

function formatLegacyUptime(ts) {
  const totalSeconds = Math.max(0, Math.floor(Number(ts || 0) / 1000));
  const minutes = Math.floor(totalSeconds / 60);
  const seconds = totalSeconds % 60;

  if (minutes <= 0) {
    return "T+" + seconds + "s";
  }

  if (minutes < 60) {
    return "T+" + minutes + "m " + seconds + "s";
  }

  const hours = Math.floor(minutes / 60);
  const remainingMinutes = minutes % 60;
  return "T+" + hours + "h " + remainingMinutes + "m";
}

function formatTimestamp(ts) {
  const timestamp = Number(ts || 0);
  if (!Number.isFinite(timestamp) || timestamp <= 0) {
    return "";
  }

  if (timestamp >= 1000000000) {
    const messageDate = new Date(timestamp * 1000);
    const now = new Date();
    const sameYear = messageDate.getFullYear() === now.getFullYear();
    const sameMonth = messageDate.getMonth() === now.getMonth();
    const sameDay = messageDate.getDate() === now.getDate();

    if (sameYear && sameMonth && sameDay) {
      return messageDate.toLocaleTimeString([], {
        hour: "2-digit",
        minute: "2-digit",
      });
    }

    const datePart = messageDate.toLocaleDateString([], sameYear
      ? {
          day: "2-digit",
          month: "2-digit",
        }
      : {
          day: "2-digit",
          month: "2-digit",
          year: "numeric",
        });

    const timePart = messageDate.toLocaleTimeString([], {
      hour: "2-digit",
      minute: "2-digit",
    });

    return datePart + " " + timePart;
  }

  return formatLegacyUptime(timestamp);
}

function saveUsername() {
  const value = (elements.username.value || "").trim();
  localStorage.setItem(STORAGE_KEY, value);
}

function updateMessageCount() {
  elements.messageCount.textContent =
    String(elements.message.value.length) + " / " + MAX_MESSAGE_LENGTH;
}

function shouldStickToBottom(container) {
  return container.scrollTop + container.clientHeight >= container.scrollHeight - 48;
}

function renderEmptyState() {
  const empty = document.createElement("div");
  empty.className = "empty-state";
  empty.textContent =
    "No messages yet. Start the room.";

  elements.messages.replaceChildren(empty);
}

function renderMessages(messages) {
  if (!Array.isArray(messages) || messages.length === 0) {
    renderEmptyState();
    return;
  }

  const isNearBottom = shouldStickToBottom(elements.messages);
  const currentUser = getUsername().toLowerCase();
  const fragment = document.createDocumentFragment();

  messages.forEach((entry) => {
    const node = elements.messageTemplate.content.firstElementChild.cloneNode(true);
    const user = entry.user || "Anonymous";
    const body = entry.msg || "";

    node.querySelector(".message-user").textContent = user;
    node.querySelector(".message-time").textContent = formatTimestamp(entry.ts);
    node.querySelector(".message-body").textContent = body;

    if (user.toLowerCase() === currentUser) {
      node.classList.add("self");
    }

    fragment.appendChild(node);
  });

  elements.messages.replaceChildren(fragment);

  if (isNearBottom) {
    elements.messages.scrollTop = elements.messages.scrollHeight;
  }
}

async function loadMessages() {
  if (state.loading) {
    return;
  }

  state.loading = true;

  try {
    const response = await fetch("/messages", {
      cache: "no-store",
      headers: {
        Accept: "application/json",
      },
    });

    if (!response.ok) {
      throw new Error("HTTP " + response.status);
    }

    const messages = await response.json();
    const snapshot = JSON.stringify(messages);

    if (snapshot !== state.lastSnapshot) {
      renderMessages(messages);
      state.lastSnapshot = snapshot;
    }

    setStatus("Connected.");
  } catch (error) {
    setStatus("Disconnected. Retrying ...");
  } finally {
    state.loading = false;
  }
}

async function submitMessage(event) {
  event.preventDefault();

  const message = elements.message.value.trim();
  if (!message) {
    return;
  }

  saveUsername();
  elements.sendButton.disabled = true;
  setStatus("Sending ...");

  try {
    const body = new URLSearchParams({
      clientTs: String(Math.floor(Date.now() / 1000)),
      user: elements.username.value.trim(),
      msg: message,
    });

    const response = await fetch("/send", {
      method: "POST",
      headers: {
        "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
      },
      body,
    });

    if (!response.ok) {
      if (response.status === 429) {
        const payload = await response.json();
        const seconds = Math.ceil((payload.retryAfterMs || 15000) / 1000);
        throw new Error("Please wait " + seconds + "s.");
      }
      throw new Error("HTTP " + response.status);
    }

    elements.message.value = "";
    updateMessageCount();
    await loadMessages();
    setStatus("Message sent.");
  } catch (error) {
    setStatus("Send failed. " + error.message);
  } finally {
    elements.sendButton.disabled = false;
  }
}

function bootstrap() {
  const storedUser = localStorage.getItem(STORAGE_KEY);
  if (storedUser) {
    elements.username.value = storedUser;
  }

  elements.historyNote.textContent =
    "// keeps the last " + MAX_STORED_MESSAGES + " messages.";
  saveUsername();
  updateMessageCount();
  renderEmptyState();

  elements.username.addEventListener("input", saveUsername);
  elements.message.addEventListener("input", updateMessageCount);
  elements.chatForm.addEventListener("submit", submitMessage);

  loadMessages();
  window.setInterval(loadMessages, POLL_INTERVAL_MS);
}

bootstrap();
