# ZOIT - Markdown 文档协作编辑系统 / Markdown Collaborative Editing System

一个基于客户端-服务器架构的 Markdown 文档实时协作编辑系统，支持多用户并发编辑和权限管理。

A real-time collaborative Markdown document editing system based on client-server architecture, supporting multi-user concurrent editing and permission management.

## 功能特性 / Features

- 📝 **实时协作编辑 / Real-time Collaborative Editing**：多个客户端可以同时编辑同一个文档 / Multiple clients can edit the same document simultaneously
- 🔐 **权限管理 / Permission Management**：基于角色的访问控制（读/写权限）/ Role-based access control (read/write permissions)
- 📋 **丰富的 Markdown 支持 / Rich Markdown Support**：标题、列表、代码块、粗体、斜体、链接等 / Headings, lists, code blocks, bold, italic, links, etc.
- 🔄 **版本控制 / Version Control**：文档版本追踪和变更通知 / Document version tracking and change notifications
- 💬 **实时广播 / Real-time Broadcasting**：所有客户端实时接收文档变更通知 / All clients receive document change notifications in real-time

## 系统要求 / System Requirements

- Linux 操作系统 / Linux operating system
- GCC 编译器（支持 C11 标准）/ GCC compiler (C11 standard support)
- pthread 库 / pthread library

## 编译 / Compilation

```bash
make
```

编译完成后会生成两个可执行文件：
After compilation, two executables will be generated:
- `server` - 服务器程序 / Server program
- `client` - 客户端程序 / Client program

清理编译文件：
Clean build files:
```bash
make clean
```

## 使用说明 / Usage Instructions

### 1. 启动服务器 / Starting the Server

首先启动服务器程序：
First, start the server program:

```bash
./server
```

服务器启动后会显示其进程 ID (PID)，例如：
The server will display its process ID (PID) after startup, for example:
```
Server PID: 12345
```

**重要 / Important**：记住这个 PID，客户端连接时需要用到。
Remember this PID, as it's needed when connecting clients.

### 2. 启动客户端 / Starting a Client

在另一个终端窗口中，使用以下命令启动客户端：
In another terminal window, start a client using the following command:

```bash
./client <server_pid> <username>
```

参数说明 / Parameter description:
- `<server_pid>`：服务器的进程 ID / Server's process ID
- `<username>`：用户名（必须在 `roles.txt` 文件中定义）/ Username (must be defined in `roles.txt` file)

示例 / Example:
```bash
./client 12345 daniel
```

### 3. 用户权限 / User Permissions

用户权限在 `roles.txt` 文件中定义，格式为：
User permissions are defined in the `roles.txt` file, with the format:
```
<username> <permission>
```

权限类型 / Permission types:
- `read`：只读权限，可以查看文档但不能编辑 / Read-only permission, can view documents but cannot edit
- `write`：读写权限，可以查看和编辑文档 / Read-write permission, can view and edit documents

示例 `roles.txt` / Example `roles.txt`:
```
ryan read
yao read
daniel write
```

### 4. 编辑命令 / Edit Commands

客户端连接成功后，会显示当前文档内容。具有写权限的用户可以输入以下命令进行编辑：
After the client connects successfully, the current document content will be displayed. Users with write permissions can enter the following commands to edit:

#### 文本编辑命令 / Text Editing Commands

- **INSERT** - 在指定位置插入文本 / Insert text at specified position
  ```
  INSERT <pos> <text>
  ```
  示例 / Example: `INSERT 0 Hello World`

- **DELETE** - 删除指定位置的文本 / Delete text at specified position
  ```
  DELETE <pos> <len>
  ```
  示例 / Example: `DELETE 0 5` （删除位置 0 开始的 5 个字符 / Delete 5 characters starting from position 0）

- **NEWLINE** - 在指定位置插入换行符 / Insert newline at specified position
  ```
  NEWLINE <pos>
  ```
  示例 / Example: `NEWLINE 10`

#### 格式化命令 / Formatting Commands

- **HEADING** - 将指定位置的行转换为标题 / Convert line at specified position to heading
  ```
  HEADING <level> <pos>
  ```
  示例 / Example: `HEADING 1 0` （将位置 0 的行转换为 1 级标题 / Convert line at position 0 to level 1 heading）

- **BOLD** - 将指定范围的文本设置为粗体 / Set text in specified range to bold
  ```
  BOLD <start> <end>
  ```
  示例 / Example: `BOLD 0 5`

- **ITALIC** - 将指定范围的文本设置为斜体 / Set text in specified range to italic
  ```
  ITALIC <start> <end>
  ```
  示例 / Example: `ITALIC 0 5`

- **CODE** - 将指定范围的文本设置为代码格式 / Set text in specified range to code format
  ```
  CODE <start> <end>
  ```
  示例 / Example: `CODE 0 10`

- **LINK** - 将指定范围的文本转换为链接 / Convert text in specified range to link
  ```
  LINK <start> <end> <url>
  ```
  示例 / Example: `LINK 0 10 https://example.com`

#### 列表和块命令 / List and Block Commands

- **ORDERED_LIST** - 将指定位置的行转换为有序列表 / Convert line at specified position to ordered list
  ```
  ORDERED_LIST <pos>
  ```
  示例 / Example: `ORDERED_LIST 0`

- **UNORDERED_LIST** - 将指定位置的行转换为无序列表 / Convert line at specified position to unordered list
  ```
  UNORDERED_LIST <pos>
  ```
  示例 / Example: `UNORDERED_LIST 0`

- **BLOCKQUOTE** - 将指定位置的行转换为引用块 / Convert line at specified position to blockquote
  ```
  BLOCKQUOTE <pos>
  ```
  示例 / Example: `BLOCKQUOTE 0`

- **HORIZONTAL_RULE** - 在指定位置插入水平分隔线 / Insert horizontal rule at specified position
  ```
  HORIZONTAL_RULE <pos>
  ```
  示例 / Example: `HORIZONTAL_RULE 0`

## 使用演示 / Usage Demo

### 演示场景：多用户协作编辑文档 / Demo Scenario: Multi-user Collaborative Document Editing

#### 步骤 1：启动服务器 / Step 1: Start the Server

终端 1 / Terminal 1:
```bash
$ ./server
Server PID: 12345
```

#### 步骤 2：启动第一个客户端（写权限用户）/ Step 2: Start First Client (Write Permission User)

终端 2 / Terminal 2:
```bash
$ ./client 12345 daniel
Document version: 0
Document length: 0
Document content:

```

客户端已连接，可以开始编辑。
Client connected, ready to start editing.

#### 步骤 3：创建文档内容 / Step 3: Create Document Content

在客户端 2 中输入命令：
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

#### 步骤 4：启动第二个客户端（只读用户）/ Step 4: Start Second Client (Read-only User)

终端 3 / Terminal 3:
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

只读用户可以看到文档内容，但无法编辑。
Read-only users can see the document content but cannot edit.

#### 步骤 5：实时协作编辑 / Step 5: Real-time Collaborative Editing

当第一个客户端（daniel）继续编辑时：
When the first client (daniel) continues editing:

```bash
# 在客户端 2 (daniel) 中输入 / Enter in client 2 (daniel):
BOLD 21 55
HEADING 2 56
```

第二个客户端（ryan）会实时收到更新通知：
The second client (ryan) will receive update notifications in real-time:
```
VERSION 4
EDIT daniel BOLD
EDIT daniel HEADING
END
```

#### 步骤 6：添加更多格式 / Step 6: Add More Formatting

在客户端 2 (daniel) 中：
In client 2 (daniel):

```bash
NEWLINE 85
INSERT 86 Here is some `code` example.
CODE 100 104
NEWLINE 130
INSERT 131 Visit our [website](https://github.com)
LINK 139 147 https://github.com
```

#### 最终文档示例 / Final Document Example

经过编辑后，文档可能看起来像这样：
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

## 命令响应 / Command Responses

- **SUCCESS**：命令执行成功 / Command executed successfully
- **Reject: <reason>**：命令被拒绝（可能原因：权限不足、版本冲突、格式错误等）/ Command rejected (possible reasons: insufficient permissions, version conflict, format error, etc.)

## 注意事项 / Important Notes

1. **版本控制 / Version Control**：每个编辑操作都会增加文档版本号，确保所有客户端同步 / Each edit operation increments the document version number to ensure all clients are synchronized
2. **权限检查 / Permission Check**：只有具有 `write` 权限的用户可以执行编辑命令 / Only users with `write` permission can execute edit commands
3. **位置索引 / Position Indexing**：所有位置参数（pos, start, end）都是基于文档的字符位置 / All position parameters (pos, start, end) are based on document character positions
4. **实时同步 / Real-time Synchronization**：所有客户端的编辑操作会实时广播给其他客户端 / All client edit operations are broadcast to other clients in real-time
5. **FIFO 通信 / FIFO Communication**：系统使用命名管道（FIFO）进行进程间通信 / The system uses named pipes (FIFO) for inter-process communication

## 项目结构 / Project Structure

```
.
├── Makefile          # 编译配置 / Build configuration
├── roles.txt         # 用户权限配置 / User permission configuration
├── libs/
│   ├── document.h    # 文档数据结构定义 / Document data structure definition
│   └── markdown.h    # Markdown 操作函数声明 / Markdown operation function declarations
└── source/
    ├── server.c      # 服务器实现 / Server implementation
    ├── client.c      # 客户端实现 / Client implementation
    └── markdown.c    # Markdown 操作实现 / Markdown operation implementation
```

## 故障排除 / Troubleshooting

### 客户端无法连接服务器 / Client Cannot Connect to Server
- 检查服务器 PID 是否正确 / Check if the server PID is correct
- 确认服务器正在运行 / Confirm the server is running
- 检查用户权限配置 / Check user permission configuration

### 命令被拒绝 / Command Rejected
- 确认用户具有 `write` 权限 / Confirm user has `write` permission
- 检查命令格式是否正确 / Check if command format is correct
- 验证位置参数是否在有效范围内 / Verify position parameters are within valid range

### 编译错误 / Compilation Errors
- 确保 GCC 支持 C11 标准 / Ensure GCC supports C11 standard
- 检查 pthread 库是否已安装 / Check if pthread library is installed
- 运行 `make clean` 后重新编译 / Run `make clean` and recompile

## 许可证 / License

本项目为课程作业项目。
This project is a course assignment.

## 贡献 / Contributing

欢迎提交 Issue 和 Pull Request！
Issues and Pull Requests are welcome!
