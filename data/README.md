# Future LittleFS web assets

The web dashboard is currently embedded in `include/web/DashboardHtml.h` so the
first Wi-Fi build has no filesystem upload step.

When the dashboard becomes larger, move the assets here, for example:

- `index.html`
- `app.js`
- `style.css`

Then add a LittleFS-backed web asset layer without changing the sensor or
telemetry architecture.
