# Удалённый доступ к тестовой Raspberry Pi

Эта инструкция описывает подключение к тестовой Raspberry Pi с другого
компьютера через Интернет. Публичный IP и проброс SSH-порта на роутере не
нужны: сетевой доступ предоставляет Tailscale, после чего используется обычный
OpenSSH.

## Параметры подключения

| Параметр | Значение |
| --- | --- |
| Имя Raspberry Pi в Tailscale | `rpi4-test` |
| Tailscale IP | `100.120.90.57` |
| Пользователь Linux | `rpi` |
| Порт SSH | `22` |

В командах предпочтительно использовать имя `rpi4-test`. IP-адрес пригодится
как запасной вариант, если разрешение имени недоступно.

Raspberry Pi обычно использует Wi-Fi и автоматически переключается на USB
LTE-модем при потере Wi-Fi. Команда подключения с другого компьютера в обоих
случаях остаётся одинаковой.

## 1. Установить Tailscale на другой компьютер

Компьютер нужно добавить в ту же сеть Tailscale (tailnet), где зарегистрирована
Raspberry Pi. Войдите под тем же аккаунтом Tailscale либо под аккаунтом, который
администратор пригласил в эту сеть.

### Windows 10 или новее

1. Скачайте и установите Tailscale:
   <https://tailscale.com/download/windows>.
2. Откройте Tailscale из области уведомлений и выберите **Log in**.
3. Завершите авторизацию в браузере.

Windows 10 и Windows 11 обычно уже содержат SSH-клиент. Проверьте его в
PowerShell:

```powershell
ssh -V
```

Если команда `ssh` не найдена, откройте PowerShell от имени администратора и
установите компонент OpenSSH Client:

```powershell
Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0
```

### Linux

Установите Tailscale официальным скриптом и подключите компьютер к tailnet:

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Откройте ссылку авторизации, которую напечатает `tailscale up`. Если в системе
нет SSH-клиента, для Debian и Ubuntu его можно установить так:

```bash
sudo apt update
sudo apt install openssh-client
```

### macOS

Установите Tailscale с <https://tailscale.com/download/mac>, разрешите создание
VPN-конфигурации и войдите в тот же tailnet. Команда `ssh` уже доступна в
Terminal поддерживаемых версий macOS.

## 2. Проверить доступность Raspberry Pi

Выполните:

```text
tailscale status
tailscale ping rpi4-test
```

Устройство `rpi4-test` должно отображаться как доступное. Tailscale может
сообщить о прямом соединении или соединении через relay — SSH работает в обоих
режимах.

Если имя не разрешается, используйте Tailscale IP:

```text
tailscale ping 100.120.90.57
```

## 3. Подключиться по SSH

В PowerShell, Windows Terminal, Linux Terminal или macOS Terminal выполните:

```text
ssh rpi@rpi4-test
```

Запасной вариант по IP:

```text
ssh rpi@100.120.90.57
```

При первом подключении SSH попросит подтвердить ключ хоста. Принимайте его
только тогда, когда подключение выполняется к `rpi4-test` или
`100.120.90.57`.

## 4. Настроить беспарольный SSH на новом компьютере

Для каждого компьютера следует создавать отдельный SSH-ключ. Не копируйте
закрытый ключ с другого компьютера.

### Создать ключ

Команда одинакова для актуальных Windows PowerShell, Linux и macOS:

```text
ssh-keygen -t ed25519
```

Нажмите Enter, чтобы сохранить ключ в стандартном месте. Для ноутбука
рекомендуется задать локальную парольную фразу. Закрытый ключ остаётся на
компьютере; на Raspberry Pi добавляется только файл с окончанием `.pub`.

### Добавить открытый ключ из Windows PowerShell

```powershell
Get-Content "$env:USERPROFILE\.ssh\id_ed25519.pub" |
    ssh rpi@rpi4-test "umask 077; mkdir -p ~/.ssh; cat >> ~/.ssh/authorized_keys"
```

Один раз введите пароль пользователя Raspberry Pi. Затем проверьте вход без
парольной аутентификации:

```powershell
ssh -o PasswordAuthentication=no rpi@rpi4-test
```

### Добавить открытый ключ из Linux или macOS

```bash
ssh-copy-id rpi@rpi4-test
ssh -o PasswordAuthentication=no rpi@rpi4-test
```

Если `ssh-copy-id` отсутствует:

```bash
cat ~/.ssh/id_ed25519.pub | \
  ssh rpi@rpi4-test 'umask 077; mkdir -p ~/.ssh; cat >> ~/.ssh/authorized_keys'
```

Если парольный вход на Raspberry Pi позднее будет отключён, передайте
администратору содержимое `.pub`-файла с нового компьютера. Администратор сможет
добавить его в `/home/rpi/.ssh/authorized_keys` с уже авторизованного ПК. Никому
не передавайте закрытый ключ `id_ed25519`.

## Необязательный короткий псевдоним SSH

Добавьте следующий блок в `~/.ssh/config` на Linux/macOS или в
`$env:USERPROFILE\.ssh\config` на Windows:

```sshconfig
Host test-rpi
    HostName rpi4-test
    User rpi
    IdentityFile ~/.ssh/id_ed25519
```

После этого достаточно команды:

```text
ssh test-rpi
```

## Устранение проблем

### `rpi4-test` отсутствует или Tailscale запрещает доступ

- Убедитесь, что Tailscale запущен на клиентском компьютере.
- Проверьте, что выполнен вход именно в нужный tailnet.
- Откройте <https://login.tailscale.com/admin/machines> и убедитесь, что новый
  компьютер и `rpi4-test` присутствуют в списке и авторизованы.
- Если используются собственные правила доступа Tailscale, администратор должен
  разрешить клиенту соединение с `rpi4-test` по TCP-порту 22.

### Tailscale ping работает, а SSH — нет

Запустите SSH с диагностикой:

```text
ssh -v rpi@rpi4-test
```

Ошибка `Permission denied` обычно означает, что открытый SSH-ключ нового
компьютера ещё не добавлен. Если после успешного Tailscale ping появляется
`Connection refused` или тайм-аут, службу SSH на Raspberry Pi нужно проверить
через serial-консоль либо с другого авторизованного компьютера.

### На Raspberry Pi нет ни Wi-Fi, ни LTE

Tailscale не может связаться с устройством без Интернет-соединения. Нужно
восстановить настроенную Wi-Fi-сеть либо подключить USB LTE-модем. Локальным
аварийным каналом остаётся serial-консоль.

## Безопасность

- Не открывайте порт 22 на мобильном роутере и не настраивайте публичный проброс
  портов: для этой схемы он не нужен.
- Обновляйте Tailscale и операционную систему.
- Используйте отдельный SSH-ключ на каждом компьютере. Ключи потерянных или
  выведенных из эксплуатации компьютеров удаляйте из
  `/home/rpi/.ssh/authorized_keys`.
- Не сохраняйте пароли, закрытые SSH-ключи и Wi-Fi-реквизиты в репозитории.

## Официальная документация

- Установка Tailscale: <https://tailscale.com/docs/install>
- Tailscale для Windows: <https://tailscale.com/docs/install/windows>
- Tailscale для Linux: <https://tailscale.com/docs/install/linux>
- Tailscale для macOS: <https://tailscale.com/docs/install/mac>
