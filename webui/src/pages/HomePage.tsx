import { useEffect } from "preact/hooks";
import { signal, computed } from "@preact/signals";
import { getEvents, getWeather } from "../api";
import type { EventsData, WeatherData } from "../types";

const weather = signal<WeatherData | null>(null);
const events = signal<EventsData | null>(null);
const error = signal<string | null>(null);

const loaded = computed(() => weather.value !== null && events.value !== null);

async function load(): Promise<void> {
  try {
    const [w, e] = await Promise.all([getWeather(), getEvents()]);
    weather.value = w;
    events.value = e;
    error.value = null;
  } catch (e) {
    error.value = String(e);
  }
}

export function HomePage() {
  useEffect(() => {
    void load();
    // Refresh every 60s — the device only re-fetches upstream every
    // minutesBetweenDataRefresh (default 15min), so polling faster is
    // wasted bandwidth without faster-changing data. 60s keeps the
    // wind/temperature roughly fresh during the few seconds after a
    // manual /api/refresh fires from the Actions tab.
    const id = setInterval(load, 60_000);
    return () => clearInterval(id);
  }, []);

  if (error.value) {
    return (
      <div>
        <p class="error-msg">{error.value}</p>
      </div>
    );
  }
  if (!loaded.value) {
    return <p class="muted">Loading…</p>;
  }

  const w = weather.value!;
  const e = events.value!;

  return (
    <div>
      <ConfigWarnings events={e} weatherValid={w.data_valid} />
      <EventsCard events={e} />
      <WeatherCard weather={w} />
    </div>
  );
}

function ConfigWarnings({
  events,
  weatherValid,
}: {
  events: EventsData;
  weatherValid: boolean;
}) {
  const items: string[] = [];
  if (!events.calendar_url_configured) items.push("calendar URL");
  if (!events.calendar_key_configured) items.push("calendar API key");
  // We don't expose a flag for the OWM key directly, but a fresh device
  // with no key set ends up with weatherValid=false and city="" — same
  // signal the legacy "Please configure Weather" warning used.
  const needWeather = !weatherValid && events.calendar_url_configured;
  if (items.length === 0 && !needWeather) return null;

  return (
    <div class="config-warnings">
      {items.length > 0 && (
        <p>
          Please configure {items.join(" and ")} on the{" "}
          <span class="muted">Settings</span> tab.
        </p>
      )}
      {needWeather && (
        <p>
          Weather not yet available — check the OpenWeatherMap key and
          city on the <span class="muted">Settings</span> tab.
        </p>
      )}
    </div>
  );
}

function EventsCard({ events }: { events: EventsData }) {
  return (
    <section class="home-section">
      <h2>Upcoming events</h2>
      {events.count === 0 ? (
        <p class="muted">No upcoming events.</p>
      ) : (
        <ul class="event-list">
          {events.messages.map((m, i) => (
            <li key={i}>{m}</li>
          ))}
        </ul>
      )}
    </section>
  );
}

function WeatherCard({ weather }: { weather: WeatherData }) {
  if (!weather.data_valid) {
    return (
      <section class="home-section">
        <h2>Weather</h2>
        <p class="muted">No weather data yet.</p>
        {weather.error_message && (
          <p class="error-msg">Error: {weather.error_message}</p>
        )}
      </section>
    );
  }
  // Round display values to match the legacy "0 decimal places" formatting.
  const t = Math.round(weather.temperature);
  const hi = Math.round(weather.temp_high);
  const lo = Math.round(weather.temp_low);
  const wind = Math.round(weather.wind_speed);
  return (
    <section class="home-section weather-card">
      <h2>
        Weather for {weather.city}
        {weather.country && `, ${weather.country}`}
      </h2>
      <div class="weather-grid">
        <div class="weather-icon-col">
          {weather.icon && (
            <img
              src={`https://openweathermap.org/img/w/${weather.icon}.png`}
              alt={weather.description}
              width={50}
              height={50}
            />
          )}
          <div class="weather-meta">
            <div>{weather.humidity}% humidity</div>
            <div>
              {weather.wind_direction_text} / {wind} {weather.speed_symbol} wind
            </div>
            <div>
              {weather.pressure} {weather.pressure_symbol}
            </div>
          </div>
        </div>
        <div class="weather-main">
          <div class="weather-condition">
            {weather.condition}
            {weather.description && (
              <span class="muted"> ({weather.description})</span>
            )}
          </div>
          <div class="weather-temp">
            {t}° {weather.temp_symbol}
          </div>
          <div class="weather-highlow muted">
            {hi}° / {lo}° {weather.temp_symbol}
          </div>
        </div>
      </div>
    </section>
  );
}
