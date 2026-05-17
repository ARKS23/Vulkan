#pragma once
#include "vk_types.h"

namespace vkutil {
    void cmdSetViewportAndScissor(VkCommandBuffer cmdBuffer, uint32_t width, uint32_t height);

    VkRenderingAttachmentInfo renderingAttachmentInfo(VkImageView imageView, VkImageLayout imageLayout, VkClearValue clearValue, 
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

    VkRenderingAttachmentInfo renderingdepthAttachmentInfo(VkImageView imageView, VkImageLayout imageLayout, float clearValue,
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE);

    void cmdBeginColorDepthRendering(
        VkCommandBuffer cmd,
        VkExtent2D extent,
        const VkRenderingAttachmentInfo& colorAttachment,
        const VkRenderingAttachmentInfo& depthAttachment);

    void cmdBeginDepthOnlyRendering(
        VkCommandBuffer cmd,
        VkExtent2D extent,
        const VkRenderingAttachmentInfo& depthAttachment);

    void cmdBeginColorOnlyRendering(
        VkCommandBuffer cmd,
        VkExtent2D extent,
        const VkRenderingAttachmentInfo& colorAttachment);

    void cmdEndRendering(VkCommandBuffer cmd);
}