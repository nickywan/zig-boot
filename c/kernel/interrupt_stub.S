.section .text
.global pure_iretq_handler
.type pure_iretq_handler, @function

pure_iretq_handler:
    iretq

.size pure_iretq_handler, . - pure_iretq_handler
