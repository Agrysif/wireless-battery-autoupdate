# Wireless Battery — сборка с автообновлением

Неофициальная сборка [Wireless Battery](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) — программы
**krisgfx9**, которая показывает заряд беспроводных мышей, наушников, клавиатур и геймпадов на панели задач Windows.
Отличие от оригинала одно: программа **сама и без вопросов обновляется** до новых версий автора.

Скачать: **[mytoolkit.ru/programmy/wireless-battery](https://mytoolkit.ru/programmy/wireless-battery)**
или установщик из [последнего релиза](../../releases/latest).

## Как это устроено

1. Раз в 2 часа GitHub Actions смотрит релизы автора. Версию, которая вышла **не меньше 12 часов назад**,
   он скачивает в исходниках, добавляет автообновление ([`patch/`](patch)), собирает, прогоняет тесты автора и свои
   и выпускает здесь как `X.Y.Z.R` (X.Y.Z — версия автора, R — номер нашей доработки).
2. Сервер mytoolkit.ru раз в час забирает релиз, сверяет SHA-256 с тем, что публикует GitHub, и подписывает
   `latest.json` ключом ECDSA P-256. Закрытый ключ есть только на сервере.
3. Программа раз в 6 часов скачивает `latest.json`, проверяет подпись, скачивает установщик, сверяет его SHA-256
   и запускает его тихо. Установщик закрывает программу, обновляет её и запускает снова. Выбор «Запускать вместе
   с Windows» сохраняется. Во время полноэкранной игры или презентации обновление ждёт.

Программа ничего не отправляет о вас или ваших устройствах: только скачивает `latest.json` и установщик.
Обновляются только копии, установленные для одного пользователя (вариант установщика по умолчанию).
Выключить: меню значка → **Обновлять автоматически**.

Если автор изменит код так, что доработка не встанет, сборка остановится с ошибкой `PATCH FAILED`:
версия без автообновления не выйдет, у всех останется прошлая.

## Сборка вручную

```
python patch/apply.py <папка с исходниками автора> 1.0.8.1
cd <папка> && make && installer/build-installer.ps1 -Version 1.0.8.1
```

---

# Wireless Battery — auto-updating build

An unofficial build of [Wireless Battery](https://github.com/krisgfx9/Wireless-Device-Battery-Monitor) by
**krisgfx9** (battery levels of wireless mice, headsets, keyboards and controllers on the Windows taskbar).
The only difference: it **updates itself silently** to new upstream versions.

- Every 2 hours GitHub Actions takes the newest upstream release that is **at least 12 hours old**, applies
  [`patch/`](patch), builds it, runs the upstream tests and ours, and publishes it here as `X.Y.Z.R`.
- mytoolkit.ru mirrors the release hourly, checks its SHA-256 against GitHub and signs `latest.json`
  with an ECDSA P-256 key that never leaves the server.
- The app checks `latest.json` every 6 hours, verifies the signature and the installer's SHA-256, and runs the
  installer silently; the installer restarts the app and keeps the "Start with Windows" choice.
- Nothing about you or your devices is sent. Only per-user installs update. Turn it off in the tray menu:
  **Update automatically**.

## License

GPL-3.0-or-later, like Wireless Battery itself ([LICENSE](LICENSE)). All credit for the app goes to krisgfx9 and
the Wireless Battery contributors; this repository only adds the updater. Every release includes the complete
patched source (`wireless-battery-<version>-source.tar.gz`) and the HeadsetControl source it bundles.
