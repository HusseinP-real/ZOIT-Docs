# ZOIT - Markdown Collaborative Editing System / Markdown 文档协作编辑系统

A real-time collaborative Markdown document editing system based on client-server architecture, supporting multi-user concurrent editing and permission management.

## Features / 功能特性

- **Real-time Collaborative Editing** - Multiple clients can edit the same document simultaneously
- **Permission Management** - Role-based access control (read/write permissions)
- **Rich Markdown Support** - Headings, lists, code blocks, bold, italic, links, etc.
- **Version Control** - Document version tracking and change notifications
- **Real-time Broadcasting** - All clients receive document change notifications in real-time

## System Requirements / 系统要求

- Linux operating system
- GCC compiler (C11 standard support)
- pthread library

## Compilation / 编译

```bash
make
```

After compilation, two executables will be generated:
- `server` - Server program
- `client` - Client program

Clean build files:
```bash
make clean
```

## Usage Instructions / 使用说明

### 1. Starting the Server / 启动服务器

First, start the server program:

```bash
./server
```

The server will display its process ID (PID) after startup, for example:
```
Server PID: 12345
```

**Important**: Remember this PID, as it's needed when connecting clients.

### 2. Starting a Client / 启动客户端

In another terminal window, start a client using the following command:

```bash
./client <server_pid> <username>
```

Parameters:
- `<server_pid>` - Server's process ID
- `<username>` - Username (must be defined in `roles.txt` file)

Example:
```bash
./client 12345 daniel
```

### 3. User Permissions / 用户权限

User permissions are defined in the `roles.txt` file, with the format:
```
<username> <permission>
```

Permission types:
- `read` - Read-only permission, can view documents but cannot edit
- `write` - Read-write permission, can view and edit documents

Example `roles.txt`:
```
ryan read
yao read
daniel write
```

### 4. Edit Commands / 编辑命令

After the client connects successfully, the current document content will be displayed. Users with write permissions can enter the following commands to edit:

#### Text Editing Commands / 文本编辑命令

- **INSERT** - Insert text at specified position
  ```
  INSERT <pos> <text>
  ```
  Example: `INSERT 0 Hello World`

- **DELETE** - Delete text at specified position
  ```
  DELETE <pos> <len>
  ```
  Example: `DELETE 0 5` (Delete 5 characters starting from position 0)

- **NEWLINE** - Insert newline at specified position
  ```
  NEWLINE <pos>
  ```
  Example: `NEWLINE 10`

#### Formatting Commands / 格式化命令

- **HEADING** - Convert line at specified position to heading
  ```
  HEADING <level> <pos>
  ```
  Example: `HEADING 1 0` (Convert line at position 0 to level 1 heading)

- **BOLD** - Set text in specified range to bold
  ```
  BOLD <start> <end>
  ```
  Example: `BOLD 0 5`

- **ITALIC** - Set text in specified range to italic
  ```
  ITALIC <start> <end>
  ```
  Example: `ITALIC 0 5`

- **CODE** - Set text in specified range to code format
  ```
  CODE <start> <end>
  ```
  Example: `CODE 0 10`

- **LINK** - Convert text in specified range to link
  ```
  LINK <start> <end> <url>
  ```
  Example: `LINK 0 10 https://example.com`

#### List and Block Commands / 列表和块命令

- **ORDERED_LIST** - Convert line at specified position to ordered list
  ```
  ORDERED_LIST <pos>
  ```
  Example: `ORDERED_LIST 0`

- **UNORDERED_LIST** - Convert line at specified position to unordered list
  ```
  UNORDERED_LIST <pos>
  ```
  Example: `UNORDERED_LIST 0`

- **BLOCKQUOTE** - Convert line at specified position to blockquote
  ```
  BLOCKQUOTE <pos>
  ```
  Example: `BLOCKQUOTE 0`

- **HORIZONTAL_RULE** - Insert horizontal rule at specified position
  ```
  HORIZONTAL_RULE <pos>
  ```
  Example: `HORIZONTAL_RULE 0`

## Usage Demo / 使用演示

### Demo Scenario: Multi-user Collaborative Document Editing / 演示场景：多用户协作编辑文档

#### Step 1: Start the Server / 步骤 1：启动服务器

Terminal 1:
```bash
$ ./server
Server PID: 12345
```

#### Step 2: Start First Client (Write Permission User) / 步骤 2：启动第一个客户端（写权限用户）

Terminal 2:
```bash
$ ./client 12345 daniel
Document version: 0
Document length: 0
Document content:

```

Client connected, ready to start editing.

#### Step 3: Create Document Content / 步骤 3：创建文档内容

Enter commands in client 2:

```bash
INSERT 0 # My First Document
NEWLINE 20
INSERT 21 This is a collaborative editing system.
NEWLINE 55
INSERT 56 ## Features
NEWLINE 70
ORDERED_LIST 21
UNORDERED_LIST 56
INSERT 57 - Real-time collaboration
NEWLINE 82
INSERT 83 - Permission management
```

#### Step 4: Start Second Client (Read-only User) / 步骤 4：启动第二个客户端（只读用户）

Terminal 3:
```bash
$ ./client 12345 ryan
Document version: 3
Document length: 85
Document content:
# My First Document
This is a collaborative editing system.
## Features
- Real-time collaboration
- Permission management
```

Read-only users can see the document content but cannot edit.

#### Step 5: Real-time Collaborative Editing / 步骤 5：实时协作编辑

When the first client (daniel) continues editing:

```bash
# Enter in client 2 (daniel):
BOLD 21 55
HEADING 2 56
```

The second client (ryan) will receive update notifications in real-time:
```
VERSION 4
EDIT daniel BOLD
EDIT daniel HEADING
END
```

#### Step 6: Add More Formatting / 步骤 6：添加更多格式

In client 2 (daniel):

```bash
NEWLINE 85
INSERT 86 Here is some `code` example.
CODE 100 104
NEWLINE 130
INSERT 131 Visit our [website](https://github.com)
LINK 139 147 https://github.com
```

#### Final Document Example / 最终文档示例

After editing, the document might look like this:

```markdown
# My First Document
**This is a collaborative editing system.**
## Features
- Real-time collaboration
- Permission management

Here is some `code` example.
Visit our [website](https://github.com)
```

## Command Responses / 命令响应

- **SUCCESS** - Command executed successfully
- **Reject: <reason>** - Command rejected (possible reasons: insufficient permissions, version conflict, format error, etc.)

## Important Notes / 注意事项

1. **Version Control** - Each edit operation increments the document version number to ensure all clients are synchronized
2. **Permission Check** - Only users with `write` permission can execute edit commands
3. **Position Indexing** - All position parameters (pos, start, end) are based on document character positions
4. **Real-time Synchronization** - All client edit operations are broadcast to other clients in real-time
5. **FIFO Communication** - The system uses named pipes (FIFO) for inter-process communication

## Project Structure / 项目结构

```
.
├── Makefile          # Build configuration
├── roles.txt         # User permission configuration
├── libs/
│   ├── document.h    # Document data structure definition
│   └── markdown.h    # Markdown operation function declarations
└── source/
    ├── server.c      # Server implementation
    ├── client.c      # Client implementation
    └── markdown.c    # Markdown operation implementation
```

## Troubleshooting / 故障排除

### Client Cannot Connect to Server / 客户端无法连接服务器
- Check if the server PID is correct
- Confirm the server is running
- Check user permission configuration

### Command Rejected / 命令被拒绝
- Confirm user has `write` permission
- Check if command format is correct
- Verify position parameters are within valid range

### Compilation Errors / 编译错误
- Ensure GCC supports C11 standard
- Check if pthread library is installed
- Run `make clean` and recompile


## Contributing / 贡献

Issues and Pull Requests are welcome!
