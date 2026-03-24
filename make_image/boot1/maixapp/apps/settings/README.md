APP settings
=======

## Get items value

You can get these config items in your APP by API in `app` module,

For C++:
```cpp
#include "maix_app.hpp"
using namespace maix;

std::string locale = app::get_sys_config_kv("language", "locale")
```

For MaixPy:
```python
from maix import app

locale = app.get_sys_config_kv("language", "locale")
```

## Config Items

```ini
[language]
locale = zh

[wifi]
ssid = hello

[comm]
method = uart # uart, tcp

[backlight]
value = 50

```

