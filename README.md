# SYSPRO_CSE4100
CSE 4100, myshell with process management, concurrent stock server
<h2>Project 1 - task 1,2,3</h2>
<h4>Main Goal</h4>
<ul>
  <li>Build my shell terminal.</li>
  <li>Execute command in child process(fork)</li>
  <li>Run and reap process on foreground/background.(with signal handling)</li>
  <li>Job(process) management with various process status(fg, bg, stop, continue)</li>
</ul>

<p>
	Details: Create example shell to learn about fork and signal, signal handling. Make job command with add, update(stopped), delete(killed -9), read. Job list shows : pid, status, bg(0 or 1). Must carefully reap fg and bg process.
</p>

<p>- 첫 번째 task에서는 기본적인 쉘을 구성하는 것으로, 간단한 입력을 받아 수행하도록 되어 있다. 문자열의 parsing이 중요하고, fork의 분기점을 정확히 이해하여 구조를 짜고, foreground job의 실행만 생각한다. process가 끝날 때까지 기다리도록 한다.<br><br>
- 두 번째 task에서는 stdout,stdin과 pipeline을 이용하여 복수의 명령을 연결하여 job을 수행할 수 있도록 한다. 문자열을 parsing하여 명령어 별로 문자열을 나눈 후에, evaluation을 실행한다. 각 파이프라인은 이전 명령어의 출력과 다음 명령어의 입력을 이어준다. <br><br>
- 세 번째 task에서는 myshell이 job들을 관리할 수 있도록 한다. 특히 중요하게 생각해야 하는 것이 signal handling을 통한 signal의 관리이다. Masking, volitle 변수 선언 등을 잘 하여 async safe 하게 함수를 작성해야 하고, child process를 waitpid를 통해 상태를 확인하여 job들을 바꿔준다.</p>


<h2>Project 2 - task 1, 2, 3</h2>
<h4>Main Goal</h4>
<ul>
  <li>Make Event-driven trade server</li>
  <li>I/O Multiplexing to respond to each request</li>
  <li>Make Thread-based concurrent stock server</li>
  <li>Thread pooling with shared buffer(Synchronization)</li>
  <li>Stock table synchronization</li>
  <li>Performance evaluation of each server.</li>
</ul>

<p>
  개발의 주 목표는 주식 서버가 concurrency를 보장하는 주식 서버라는 것에 있다. 프로세스 기반, 이벤트 기반, 스레드 기반이 있는데, 이 중 이벤트 기반과 스레드 기반으로 이루어진 서버를 구축한다. tcp socket과 그 주소, connect, listen, accept등에 대해 이해한 상태로, 베이스코드 내에서 동시성을 구현한다.
주식 서버가 하는 일은 사용자와 연결되어 사용자의 입력을 받고, 해당하는 요청을 메모리의 주식 정보와 비교하여 수행한다. 주식 정보를 조회하고, 판매하고, 구매할 수 있도록 한다. 단, 앞서 밝힌 대로 이는 I/O Multiplexing, thread를 통해 구현되어야 한다.
</p>


<p>
by. Hyeonseok Kang <br>
	- project from CSE4100, Prof. Youngjae Kim, Sogang Univ. <br>
  - csapp base code from Computer Systems(A programmer's perspective, 3rd Edition), Randal E. Bryant, David R. O'Hallaron
</p>
