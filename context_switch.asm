; switch_context(void** current_stack, void* next_stack)
; rdi -> eski stack pointer'ın kaydedileceği adres
; rsi -> yeni stack pointer'ın olduğu adres

global switch_context
switch_context:
    ; 1. Mevcut tüm register'ları stack'e yedekle
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; 2. Eski stack pointer'ı rdi'nin gösterdiği yere kaydet
    mov [rdi], rsp

    ; 3. İşlemciye "Senin yeni stack'in rsi'daki adrestir" de
    mov rsp, rsi

    ; 4. Yeni stack'ten yedekleri geri yükle
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ; 5. Yeni yerdeki fonksiyona 'merhaba' de
    ret
