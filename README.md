# iPhone USB Camera для OBS

Прототип для iPhone 7 и Windows. В OBS добавляется источник **iPhone USB Camera**. Плагин подключается к приложению на iPhone через USB-службу Apple (`usbmuxd`) и передаёт кадры в OBS напрямую. HTTP-сервера, адреса браузера и Wi-Fi в цепочке OBS нет.

## Состав

- `ios/iPhoneUSBObs.xcodeproj` — приложение iPhone (iOS 15+), захват 720p/15 fps и передача JPEG-кадров.
- `obs-plugin` — модуль OBS для Windows. Принимает кадры через USB и декодирует их штатным Windows Imaging Component.

## Сборка iPhone

**Без Mac:** загрузите проект в репозиторий GitHub, откройте Actions → **Build unsigned iPhone app** → **Run workflow** и скачайте артефакт `iPhoneUSBObs-unsigned-ipa`. Внутри архива находится файл `.ipa`. Установите его на iPhone с Windows через [Sideloadly](https://sideloadly.io/), выбрав режим Apple ID Sideload. Нужны обычный Apple ID и версия iTunes/iCloud для Windows с сайта Apple, как указано на сайте Sideloadly. Пароль Apple ID вводится только в Sideloadly, не в GitHub или в этот проект. На телефоне разрешите доступ к камере и нажмите **Начать**. Экран должен оставаться разблокированным, приложение — открытым.

**С Mac:** откройте `ios/iPhoneUSBObs.xcodeproj`, выберите свою команду подписи в **Signing & Capabilities**, при необходимости измените Bundle Identifier и запустите приложение на iPhone 7.

## Сборка плагина Windows

**Без Visual Studio:** в GitHub Actions запустите **Build OBS plugin for Windows** и скачайте артефакт `iphone-usb-camera-obs33-windows`. Облачная сборка использует официальную заготовку плагина OBS и её зависимости.

**Локально:** нужны Visual Studio с C++ и CMake, а также пакет разработки `libobs` для версии установленного OBS Studio. Из каталога `obs-plugin`:

```powershell
cmake -S . -B build -A x64 -Dlibobs_DIR="C:\path\to\libobs\cmake"
cmake --build build --config Release
```

Для OBS 33 поместите `iphone-usb-camera.dll` в `%ProgramData%\obs-studio\plugins\iphone-usb-camera\iphone-usb-camera.dll` и перезапустите OBS. Установите Apple Devices или iTunes, чтобы работала Apple Mobile Device Service. Подключите iPhone кабелем Lightning, подтвердите **Доверять этому компьютеру**, откройте приложение и добавьте в OBS источник **iPhone USB Camera**.

## Текущие ограничения

- Передаётся только видео, без звука с iPhone.
- Подключается первый найденный iPhone по USB.
- JPEG 720p/15 fps — простой вариант для первого рабочего прототипа; задержка и нагрузка ещё не измерены на реальном iPhone 7.
- Приложение и плагин пока не собраны и не проверены вместе на реальном устройстве.
