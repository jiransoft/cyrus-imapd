# Cyrus IMAPD Cassandane Docker 테스트 환경

Docker 환경에서 Cyrus IMAPD를 빌드하고 Cassandane 테스트를 실행하기 위한 가이드입니다.

## 사전 요구사항

- Docker Desktop 설치 및 실행
- Docker Compose 설치

## 디렉토리 구조

```
cyrus-imapd/
├── cassandane/
│   ├── Dockerfile              # Docker 이미지 정의
│   ├── docker-compose.yml      # Docker Compose 설정
│   ├── docker-README.md        # 이 문서
│   ├── cassandane.ini          # Cassandane 설정
│   └── utils/                  # Cassandane 유틸리티
└── imap/                       # Cyrus IMAPD 소스 코드
```

## 빠른 시작 가이드

### 1. Cassandane 설정 파일 생성

처음 사용하는 경우, cassandane.ini 파일을 생성해야 합니다:

```bash
# cassandane 디렉토리로 이동
cd cassandane

# 예제 파일을 복사하여 cassandane.ini 생성
cp cassandane.ini.example cassandane.ini

# cassandane.ini 편집 (prefix 설정)
# [cyrus default] 섹션에서 다음 줄을 찾아서:
# ##prefix = /usr/cyrus
# 주석을 제거하고 다음과 같이 수정:
# prefix = /usr/local
```

또는 다음 명령으로 자동 생성:

```bash
cat > cassandane.ini << 'EOF'
[cyrus default]
prefix = /usr/local
EOF
```

**참고:** Docker 환경에서는 대부분의 설정을 환경 변수로 오버라이드하므로, `prefix` 설정만 있으면 충분합니다.

### 2. Docker 환경 설정

```bash
# Docker 이미지 빌드
docker-compose build

# Docker 컨테이너 시작
docker-compose up -d

# 컨테이너 접속
docker-compose exec cyrus-dev bash
```

### 3. Cyrus IMAPD 빌드

컨테이너 내부에서 다음 명령을 실행합니다:

```bash
# 1. Configure 스크립트 생성 (git 소스인 경우)
cd /srv/cyrus-imapd
autoreconf -i

# 2. Configure 실행
./configure \
  --enable-unit-tests \
  --enable-xapian \
  --enable-http \
  --enable-jmap \
  --enable-calalarmd \
  --enable-replication \
  --enable-nntp \
  --enable-murder \
  --enable-idled \
  --enable-autocreate \
  --enable-backup \
  --enable-silent-rules \
  --without-zephyr

# 3. 빌드 (병렬 컴파일)
make -j$(nproc)

# 4. 설치
sudo make install

# 5. 공유 라이브러리 경로 설정
sudo bash -c 'echo /usr/local/lib > /etc/ld.so.conf.d/cyrus.conf'
sudo ldconfig
```

### 4. 테스트 환경 준비

```bash
# 1. 필수 Perl 모듈 설치
sudo cpanm --notest \
  Plack \
  Mail::IMAPTalk \
  Math::Int64 \
  Math::Int128 \
  Clone::Choose \
  Test::MockModule \
  Test::MockObject \
  IO::File::fcntl \
  Data::GUID \
  Digest::CRC

# 2. net-tools 설치 (netstat 필요)
sudo apt-get update
sudo apt-get install -y net-tools

# 3. Cassandane utils 빌드
cd /srv/cyrus-imapd/cassandane/utils
make clean && make
```

### 5. 테스트 실행

```bash
# cassandane 디렉토리로 이동
cd /srv/cyrus-imapd/cassandane

# 특정 테스트 실행 (예: email-import-allowduplicate)
./testrunner.pl Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate

# 모든 JMAPEmail 테스트 실행
./testrunner.pl Cassandane::Cyrus::JMAPEmail

# Verbose 모드로 실행
./testrunner.pl -v Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate

# Pretty 포맷으로 실행
./testrunner.pl -f pretty Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate
```

## 컨테이너 외부에서 실행하기

Docker 컨테이너에 접속하지 않고 호스트에서 직접 명령을 실행할 수 있습니다:

```bash
# Cyrus 빌드
docker-compose exec -T cyrus-dev bash -c "
  cd /srv/cyrus-imapd && \
  autoreconf -i && \
  ./configure --enable-unit-tests --enable-xapian --enable-http --enable-jmap --enable-calalarmd --enable-replication --enable-nntp --enable-murder --enable-idled --enable-autocreate --enable-backup --enable-silent-rules --without-zephyr && \
  make -j\$(nproc) && \
  sudo make install && \
  sudo bash -c 'echo /usr/local/lib > /etc/ld.so.conf.d/cyrus.conf' && \
  sudo ldconfig
"

# 테스트 실행
docker-compose exec -T cyrus-dev bash -c "
  cd /srv/cyrus-imapd/cassandane && \
  ./testrunner.pl Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate
"
```

## 수정된 코드 테스트하기

로컬에서 코드를 수정한 후:

```bash
# 1. 로컬에서 코드 수정 (예: imap/jmap_mail.c)

# 2. 컨테이너에서 재빌드
docker-compose exec cyrus-dev bash -c "cd /srv/cyrus-imapd && make -j\$(nproc) && sudo make install"

# 3. 테스트 실행
docker-compose exec cyrus-dev bash -c "cd /srv/cyrus-imapd/cassandane && ./testrunner.pl Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate"
```

## Docker 환경 세부사항

### 컨테이너 설정

- **베이스 이미지**: Debian Bookworm
- **사용자**: cyrus (uid:gid 1000:1000) - non-root 실행
- **작업 디렉토리**: /srv/cyrus-imapd
- **마운트 경로**:
  - 호스트 `../` → 컨테이너 `/srv/cyrus-imapd`
  - 호스트 `./` → 컨테이너 `/srv/cyrus-imapd/cassandane`

### 환경 변수

- `CASSINI_CASSANDANE_ROOTDIR=/tmp/cass` - 테스트 인스턴스 루트 디렉토리
- `CASSINI_CASSANDANE_CLEANUP=no` - 테스트 후 자동 정리 비활성화
- `TMPDIR=/tmp` - Test2 임시 디렉토리
- `HOME=/home/cyrus` - 사용자 홈 디렉토리
- `USER=cyrus` - 현재 사용자명
- `PERL5LIB=/srv/cyrus-imapd/cassandane` - Perl 모듈 검색 경로

### 네트워크 설정

- **IPv6 지원**: 활성화
- **서브넷**:
  - IPv4: 172.20.0.0/16
  - IPv6: fd00::/80

## 컨테이너 관리

```bash
# 컨테이너 상태 확인
docker-compose ps

# 컨테이너 로그 확인
docker-compose logs cyrus-dev

# 컨테이너 중지
docker-compose stop

# 컨테이너 시작
docker-compose start

# 컨테이너 재시작
docker-compose restart

# 컨테이너 및 볼륨 삭제
docker-compose down -v

# 이미지까지 모두 삭제
docker-compose down -v --rmi all
```

## 설치된 의존성

### 빌드 도구
- autoconf, automake, libtool
- gcc, g++, make, pkg-config
- git, vim, gdb, valgrind
- bison, flex

### Cyrus IMAPD 라이브러리
- libssl-dev, libsasl2-dev
- libpcre2-dev, libjansson-dev
- libical-dev, libxml2-dev
- libsqlite3-dev, libcunit1-dev
- libxapian-dev, libdb-dev
- libicu-dev, libldap2-dev

### Perl 모듈 (Debian 패키지)
- perl, perl-doc, cpanminus
- libanyevent-perl, libbsd-resource-perl
- libclone-perl, libconfig-inifiles-perl
- libdatetime-perl, libdbi-perl
- libjson-perl, libjson-xs-perl
- libmail-imapclient-perl
- libtest-unit-perl
- 기타 다수

### Perl 모듈 (CPAN)
- Net::CalDAVTalk, Net::CardDAVTalk
- Mail::JMAPTalk
- Data::ICal, Text::VCardFast
- XML::Spice, XML::Fast
- Plack, Mail::IMAPTalk
- Math::Int64, Math::Int128
- Clone::Choose
- Test::MockModule, Test::MockObject
- IO::File::fcntl
- Data::GUID, Digest::CRC

### 시스템 도구
- net-tools (netstat)
- SASL 라이브러리 및 모듈

## Cassandane 설정 파일 (cassandane.ini)

### 설정 파일 생성 방법

Cassandane은 `cassandane.ini` 파일을 통해 설정을 관리합니다. 다음 세 가지 방법 중 하나를 선택할 수 있습니다:

#### 방법 1: 예제 파일 복사 (권장)

```bash
cd cassandane
cp cassandane.ini.example cassandane.ini

# 편집기로 열어서 [cyrus default] 섹션의 prefix 수정
# ##prefix = /usr/cyrus → prefix = /usr/local
```

#### 방법 2: 최소 설정 파일 직접 생성

```bash
cd cassandane
cat > cassandane.ini << 'EOF'
[cyrus default]
prefix = /usr/local
EOF
```

#### 방법 3: 환경 변수만 사용 (파일 없이)

cassandane.ini 파일이 없어도 모든 설정을 환경 변수로 오버라이드할 수 있습니다:

```bash
export CASSINI_CASSANDANE_ROOTDIR=/tmp/cass
export CASSINI_CASSANDANE_CLEANUP=no
export CASSINI_CYRUS_DEFAULT_PREFIX=/usr/local

./testrunner.pl <테스트명>
```

### 설정 우선순위

```
환경 변수 > cassandane.ini > 기본값
```

### Docker 환경에서의 설정

Docker Compose 환경에서는 대부분의 설정을 `docker-compose.yml`의 환경 변수로 관리합니다:

```yaml
environment:
  - CASSINI_CASSANDANE_ROOTDIR=/tmp/cass
  - CASSINI_CASSANDANE_CLEANUP=no
  - TMPDIR=/tmp
  - HOME=/home/cyrus
  - USER=cyrus
  - PERL5LIB=/srv/cyrus-imapd/cassandane
```

따라서 Docker 환경에서는 `cassandane.ini` 파일에 `prefix` 설정만 있어도 충분합니다.

### 주요 설정 항목

- **prefix**: Cyrus IMAPD 설치 경로 (예: `/usr/local`)
- **rootdir**: 테스트 인스턴스 디렉토리 (예: `/tmp/cass`)
- **cleanup**: 테스트 후 정리 여부 (`yes` 또는 `no`)
- **base_port**: 테스트 서비스 시작 포트 (기본값: `9100`)

## 문제 해결

### 1. Docker 데몬 오류

```bash
# Docker Desktop이 실행 중인지 확인
docker ps
```

### 2. 빌드 오류: sieve/addr.c

sieve/addr.y 파일의 Bison 호환성 문제인 경우, 이미 패치되어 있어야 합니다.
문제가 지속되면 `%param`을 `%parse-param`과 `%lex-param`으로 분리하세요.

### 3. 공유 라이브러리 오류

```bash
docker-compose exec cyrus-dev bash -c "
  sudo bash -c 'echo /usr/local/lib > /etc/ld.so.conf.d/cyrus.conf' && \
  sudo ldconfig
"
```

### 4. Perl 모듈 누락

```bash
# 특정 모듈 설치
docker-compose exec cyrus-dev bash -c "sudo cpanm --notest <모듈명>"

# 예: Plack 설치
docker-compose exec cyrus-dev bash -c "sudo cpanm --notest Plack"
```

### 5. syslog_probe 실행 오류

macOS에서 빌드한 바이너리가 Linux 컨테이너에 마운트되어 있는 경우:

```bash
docker-compose exec cyrus-dev bash -c "
  cd /srv/cyrus-imapd/cassandane/utils && \
  make clean && make
"
```

### 6. netstat 명령 없음

```bash
docker-compose exec cyrus-dev bash -c "
  sudo apt-get update && \
  sudo apt-get install -y net-tools
"
```

### 7. Test2 IPC 권한 오류

컨테이너가 non-root 사용자로 실행되도록 설정되어 있습니다.
여전히 문제가 있다면:

```bash
# 임시 디렉토리 정리
docker-compose exec cyrus-dev bash -c "rm -rf /tmp/test2*"
```

### 8. cassandane.ini 파일 관련 오류

`unable to locate master binary` 또는 바이너리를 찾을 수 없다는 오류가 발생하는 경우:

```bash
# cassandane.ini 파일이 있는지 확인
ls -la cassandane/cassandane.ini

# 없다면 생성
cd cassandane
cat > cassandane.ini << 'EOF'
[cyrus default]
prefix = /usr/local
EOF

# prefix 설정이 올바른지 확인
grep prefix cassandane/cassandane.ini
# 출력: prefix = /usr/local
```

### 9. 테스트 디버깅

```bash
# 상세 로그와 함께 테스트 실행
docker-compose exec cyrus-dev bash -c "
  cd /srv/cyrus-imapd/cassandane && \
  ./testrunner.pl -v -f pretty Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate
"

# 테스트 인스턴스 디렉토리 확인
docker-compose exec cyrus-dev bash -c "ls -la /tmp/cass/"

# syslog 확인
docker-compose exec cyrus-dev bash -c "grep -i 'email/import' /tmp/cass/*/conf/log/syslog"

# reconstruct 출력 확인
docker-compose exec cyrus-dev bash -c "cat /tmp/cass/*/reconstruct.out"
```

## 알려진 이슈

### INCONSISTENCIES FOUND IN SPOOL

일부 테스트에서 `INCONSISTENCIES FOUND IN SPOOL` 오류가 발생할 수 있습니다.
이는 Cassandane의 엄격한 검증 때문이며, 대부분의 경우 테스트 기능 자체는 정상 작동합니다.

reconstruct.out에서 "setting internaldate from Date header" 메시지가 나타나는 것은
정상이며, 실제 기능에는 영향을 주지 않습니다.

테스트가 의도한 대로 작동했는지 확인하려면:
- syslog에서 JMAP 호출이 HTTP 200 OK로 성공했는지 확인
- reconstruct.out에서 예상한 수의 이메일이 생성되었는지 확인

## 참고 자료

- [Cyrus IMAPD 공식 문서](https://www.cyrusimap.org/)
- [Cassandane GitHub](https://github.com/cyrusimap/cassandane)
- [Cyrus Docker GitHub](https://github.com/cyrusimap/cyrus-docker)
- [CLAUDE.md](../CLAUDE.md) - 프로젝트 빌드 및 테스트 가이드

## 테스트 예제: email-import-allowduplicate

현재 브랜치(9folders-dev)에서 추가한 `allowDuplicate` 기능을 테스트하는 예제입니다.

### 테스트 실행

```bash
docker-compose exec cyrus-dev bash -c "
  cd /srv/cyrus-imapd/cassandane && \
  rm -rf /tmp/cass/* && \
  ./testrunner.pl Cassandane::Cyrus::JMAPEmail.email_import_allowduplicate
"
```

### 테스트 검증

```bash
# 1. reconstruct 출력 확인 (drafts에 3개의 이메일이 생성되어야 함)
docker-compose exec cyrus-dev bash -c "cat /tmp/cass/*/reconstruct.out"

# 예상 출력:
# user.cassandane.drafts uid 1 ...
# user.cassandane.drafts uid 2 ...
# user.cassandane.drafts uid 3 ...

# 2. syslog에서 Email/import 호출 확인 (4번 호출, 모두 200 OK)
docker-compose exec cyrus-dev bash -c "grep -i 'email/import' /tmp/cass/*/conf/log/syslog"

# 예상 출력:
# ... "POST /jmap/ HTTP/1.1" (auth=Basic; jmap=Email/import) => "HTTP/1.1 200 OK" ...
# (4번 반복)
```

### 성공 기준

- Email/import가 4번 호출됨 (모두 HTTP 200 OK)
- drafts 폴더에 3개의 중복 이메일이 생성됨 (uid 1, 2, 3)
- allowDuplicate=true일 때 중복 이메일 생성 허용
- allowDuplicate=false일 때 alreadyExists 오류 반환

## 라이선스

이 프로젝트는 Cyrus IMAPD의 라이선스를 따릅니다.
