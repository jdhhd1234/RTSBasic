# 하이브리드 RL AI

## 빌드

프로젝트 루트에서 `build_rl.bat`를 실행한다. MSVC, Ninja와 Release용 LibTorch가 필요하다. CUDA용 LibTorch를 쓰려면 해당 버전의 CUDA Toolkit도 설치한다. 기본 LibTorch 경로는 `C:\Users\kym10\Desktop\libtorch`이며, 다른 경로는 `LIBTORCH_DIR` 환경 변수에 지정한다. 빌드 스크립트는 `build\_deps`에 내려받은 의존성 소스를 재사용하며 실행 파일과 DLL을 `build_rl`에 둔다.

## 학습

프로젝트 루트에서 다음 명령을 실행한다.

```bat
build_rl\minrts_train.exe --iterations 2 --episodes-per-iter 4 --seconds 300
build_rl\minrts_train.exe --iterations 10 --resume data\ai\rl_policy.pt --threads 4 --seed 9
```

시나리오는 실행 파일 옆의 `data/sandbox/bot_battle.lua`를 읽는다. 기본 경기 시간은 게임 내 900초다. 학습 측은 매 경기 청군과 적군을 번갈아 맡고 상대는 Utility AI로 플레이한다. 워커는 CPU에서 경기를 돌리며 PPO 업데이트에는 CUDA가 있으면 CUDA를 사용한다.

매 반복이 끝날 때 작업 디렉터리 기준 `data/ai/rl_policy.pt`에 모델을 저장한다. `--out`으로 저장 위치를 바꿀 수 있다. `--resume`은 모델 가중치를 불러와 새 Adam 상태로 학습을 이어간다. 게임에서 정책을 연결할 때는 실행 파일 옆 `data/ai/rl_policy.pt`에 모델을 두고 `MakeRlInferenceHook(data, checkpoint)`를 대상 진영의 `policy`에 지정한다.

## 동작 원리

Utility AI가 사단별 행동 후보와 기존 점수를 만든다. Actor는 각 후보에 보정값을 더한다. 최종 logit은 `utility / 0.1 + actor(features)`다. Actor의 마지막 층을 0으로 초기화하므로 첫 학습 전에는 기존 점수의 softmax가 그대로 적용되고, 최고 logit의 후보는 Utility AI가 고르는 후보와 같다. Critic은 진영 전체 특징으로 상태 가치를 예측한다. PPO는 같은 판단 주기에 선택한 사단 행동의 로그 확률을 합산한다. 학습 시에는 후보를 확률적으로 선택하며 CPU 추론 훅은 최고 logit의 후보를 선택한다.
