# Servidor y Cliente Web — Robot Aspiradora Autónomo (CE-1113)

Servidor embebido en C utilizando la biblioteca **Mongoose** y cliente web interactivo en tiempo real mediante **JSON-RPC 2.0 sobre WebSockets**, con cifrado de credenciales de extremo a extremo y mapa de ruta 2D.

---

## 1. Estructura de Directorios

```
Robot-Aspiradora-Yocto/
├── server/                     # Servidor en C (Mongoose)
│   ├── CMakeLists.txt          # Configuración de compilación para Yocto / CMake
│   ├── Makefile                # Build local rápido con make
│   ├── src/
│   │   ├── main.c              # Punto de entrada, bucle Mongoose, HTTP y WebSockets
│   │   ├── rpc_handlers.c      # Métodos JSON-RPC 2.0 (auth, robot, audio, map)
│   │   ├── rpc_handlers.h
│   │   ├── auth.c              # Autenticación, sesiones y almacenamiento cifrado local
│   │   ├── auth.h
│   │   ├── crypto_util.c       # Cifrado/Descifrado AES-256-CBC con OpenSSL EVP
│   │   ├── crypto_util.h
│   │   ├── sim_robot.c         # Máquina de estados simulada, sensores y odometría 2D
│   │   └── sim_robot.h
│   └── mongoose/               # Mongoose v7.15 (mongoose.c y mongoose.h)
│
├── client/                     # Dashboard Web del Cliente (HTML5 / CSS / JS)
│   ├── index.html              # Aplicación web de página única (SPA)
│   ├── css/
│   │   └── style.css           # Estilos responsivos con tema dark/cyber
│   └── js/
│       ├── crypto.js           # Cifrado en cliente con Web Crypto API (AES-256-CBC)
│       ├── rpc.js              # Cliente JSON-RPC 2.0 sobre WebSocket
│       └── app.js              # Controlador UI y renderizado del mapa 2D en Canvas
│
└── tests/
    └── test_client.py          # Suite de pruebas automatizadas end-to-end
```

---

## 2. Compilación y Ejecución

### Compilar el Servidor
Desde el directorio `server/`:
```bash
make
```

### Ejecutar el Servidor
```bash
# Desde la raíz del repositorio:
./server/robot-server -p 8080 -r ./client
```

Argumentos disponibles:
- `-p <puerto>`: Puerto de escucha HTTP/WS (por defecto `8080`).
- `-r <ruta>`: Directorio raíz de archivos estáticos del cliente (por defecto `../client` o `./client`).
- `-d <ruta_db>`: Archivo de base de datos de usuarios (por defecto `users.db`).

### Acceder al Cliente
Abre tu navegador en:
```
http://localhost:8080
```
*(O desde tu teléfono/tablet en la misma red Wi-Fi: `http://<IP_DE_LA_RASPBERRY>:8080`)*

---

## 3. Credenciales y Seguridad

### Usuario por defecto
- **Usuario:** `admin`
- **Contraseña:** `admin123`

### Registro de nuevos usuarios
- La pantalla inicial permite alternar entre **Iniciar Sesión** y **Registrarse**.
- Las credenciales se cifran **en el navegador** mediante la **Web Crypto API** nativa (`AES-256-CBC` con vector de inicialización aleatorio `IV`).
- El servidor recibe el payload cifrado en base64, lo descifra para validarlo o registrarlo, y almacena las credenciales **cifradas localmente** en `users.db`.

---

## 4. Pruebas Automatizadas

Se incluye una suite de pruebas completa en Python que verifica las 9 funcionalidades clave:
```bash
python3 tests/test_client.py
```

Pruebas cubiertas:
1. Entrega de archivos estáticos HTTP (`index.html`).
2. Handshake y actualización a WebSocket en `/websocket`.
3. Inicio de sesión cifrado con credenciales predeterminadas.
4. Registro de nuevo usuario con cifrado y posterior login exitoso.
5. Cambio de modo de operación (Autónomo $\leftrightarrow$ Manual).
6. Control de tracción y motor de succión (vacuum).
7. Control de reproducción de audio y activación de sonidos de notificación (Task 3.3).
8. Consulta y estructura de matriz del mapa de ruta 2D (`map.get`).
9. Recepción del flujo continuo de telemetría a 5 Hz (`robot.telemetry` y `robot.map_update`).
