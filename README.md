# 환경 설정 파일
## aarch64 docker에서 PintOS 시작하기
이 글을 PintOS 프로젝트가 시작된 지 대략 3주만에 작성하는 이유는 아래와 같습니다.

1. AWS 등의 클라우드 환경에서 원격 디버거가 자주 프리징됩니다.
2. 원격 데스크탑을 사용했을 때 환경이 달라 잦은 작업 전환이 힘듭니다.
3. x86_64 아키텍처의 docker(Ubuntu)에서 Sonoma 업데이트 이후에 qemu 실행이 불가능합니다.
4. 가상 머신을 이용하는 경우 알 수 없는 이유로 가상 머신이 갑자기 꺼집니다.

특히 3번의 경우 qemu 실행 시 `rosetta error: Unimplemented syscall number 282` 오류가 발생하는 문제가 있었습니다. x86_64 애뮬레이트를 할 때 docker는 macOS에서 Rosetta 2 위에서 작동하는데, 이와 관련된 문제이므로 aarch64 아키텍쳐로 바꾸어주면 문제가 없을 것입니다. 그리고 컴파일 시 x86_64로 크로스 컴파일하고, qemu를 이용하면 됩니다.

### 크로스 컴파일 환경 설정
1. `gcc-multilib-arm-linux-gnueabi`를 설치합니다.
2. 크로스 컴파일러 `gcc-x86-64-linux-gnu`를 설치합니다.
3. 멀티 아키텍쳐 디버거 `gdb-multiarch`를 설치합니다.
4. `/usr/bin/x86_64-linux-gnu-`로 시작하는 모든 실행파일에 대해 링크를 생성합니다.
5. `CC`, `OBJDUMP`, `OBJCOPY`, `LD` 등 환경 변수를 설정합니다.

이 과정을 거치면 크로스 컴파일이 가능합니다. PintOS의 Makefile을 수정하지 않기 위해서는 위 과정을 거쳐야합니다. 

### 요약
다음은 상기 과정이 담긴 `Dockerfile`입니다.

``` dockerfile
# Dockerfile for setting up the development environment on aarch64
FROM ubuntu:18.04

RUN apt-get update && \
    apt-get install -y python3 \
    make \
    git \
    gdb \
    gdb-multiarch \
    gcc-multilib-arm-linux-gnueabi \
    gcc-x86-64-linux-gnu \
    g++-x86-64-linux-gnu \
    qemu;

RUN for file in /usr/bin/x86_64-linux-gnu-*; do \
        link_name=$(basename $file | sed 's|x86_64-linux-gnu-||'); \
        ln -s $file /usr/bin/$link_name; \
    done

ENV CROSS_COMPILE x86_64-linux-gnu-
ENV CC ${CROSS_COMPILE}gcc
ENV CXX ${CROSS_COMPILE}g++
ENV LD ${CROSS_COMPILE}ld
ENV AR ${CROSS_COMPILE}ar
ENV AS ${CROSS_COMPILE}as
ENV RANLIB ${CROSS_COMPILE}ranlib
ENV NM ${CROSS_COMPILE}nm
ENV OBJCOPY ${CROSS_COMPILE}objcopy
ENV OBJDUMP ${CROSS_COMPILE}objdump
ENV STRIP ${CROSS_COMPILE}strip
```

상기 `Dockerfile`을 저장한 뒤 다음 지침에 따르세요.

1. Docker Desktop을 설치하고 실행하세요. Docker Desktop 좌측 하단에 Docker Engine이 Running 상태임을 확인하세요. 그리고 터미널을 켜 아래 명령어를 타이핑합니다.
2. 해당 Dockerfile이 있는 경로로 이동합니다. 디렉토리에 Dockerfile 하나만 존재하는 것이 좋습니다.
3. `docker build -t <image_name> .`
4. `docker run -it --name <container_name> <image_name>`
5. 터미널을 종료하고 컨테이너를 재시작합니다.
6. 만약 Visual Studio Code를 사용하면 Remote Development, Docker 확장을 설치하고 Attach Visual Studio Code를 해보세요.
7. 컨테이너를 삭제하고 다시 만들 때 마다 레포지토리를 다시 받아오고 싶지 않다면 docker volume을 사용하세요.

### 기타
디버깅은 `gdb`가 아니라 `gdb-multiarch`를 이용합니다. 그리고 절대 gcc 혹은 build-essential을 설치하지 마세요. 심볼릭 링크 만든 것을 덮어 씌워버립니다!
